#include "session/AgvSession.h"
#include "manager/WorldManager.h" //更新数字地图全景的状态 : 观测世界
#include "manager/TaskManager.h"  //更新数字任务全景的状态 ：决策世界
#include "utils/Logger.h"
#include <string>
#include "session/AgvManager.h"
#include <myreactor/ThreadPool.h>
#include "myreactor/Timestamp.h"




namespace agv{
namespace session{

using namespace protocol; 
using namespace model;

AgvSession::AgvSession(spConnection conn,  myreactor::ThreadPool& pool): conn_(conn), workerPool_(pool) {} 

AgvSession::~AgvSession() {
    LOG_INFO("Session Destoryed. AGV ID: %d", agvId_);
}

// 登录处理 : 收到 LoginRequest -> 注册到 WorldManager -> 回复 LoginResponse
// 阶段二：业务请求处理
void AgvSession::HandleLogin(const LoginRequest& req, int32_t seq) {
    // 自我状态检查 (防同连接重复) ：如果isLogin是true，则说明肯定经过handlogin，直接退出算是优化
    if(isLogin_) {
        LOG_WARN("Duplicate Login Request from AGV %d", agvId_);
        return;
    }

    /*工业级做法
        在 补全匿名session 之前，需要加入 鉴权逻辑 (Authentication)：
        查库：拿着 req.agvId 去查询数据库（MySQL/Redis），找到该 AGV 预设的密码哈希值。
        比对：将 req.password 进行 Hash 运算后与数据库中的值比对。
    */
    const std::string MOCK_PWD = "123456"; // 设所有车密码都是 123456

    if (req.password != MOCK_PWD) {
        LOG_WARN("Login Failed: AGV %d wrong password %s", req.agvId, req.password.c_str());
        
        LoginResponse resp;
        resp.success = false;
        resp.token = "";
        resp.message = "Invalid Password";
        Send(MsgType::LOGIN_RESP, resp, seq);
        return; // 不注册，还要断开连接
    }

    // 全局状态检查 (顶号策略 / 踢旧连接) ： 忽略新的策略是危险的
    /*“后来者居上”原则 ： “会话抢占” 或 “强制下线旧会话”
    旧 Session = 断网前建立的连接（可能已经成了半死不活的僵尸）。
    新 Session = 网络恢复后重新发起的新连接
    */
    auto oldSession = AgvMgr.GetSession(req.agvId);
    if(oldSession != nullptr) {
        LOG_WARN("Kick-off: AGV %d logged in from new conn. Kicking old session...", req.agvId);
        AgvMgr.KickAgv(req.agvId);
    }

    // 补全匿名session
    isLogin_ = true;
    agvId_ = req.agvId;

    // 注册
    // 先 基础设施注册 
    AgvMgr.RegisterAgvId(agvId_, shared_from_this());
    // 后 上层业务注册
    WorldMgr.OnAgvLogin(req);
    
    // 构造回复
    LoginResponse resp;
    resp.success = true;
    resp.token = "TOKEN_" + std::to_string(agvId_); // 简单表示
    resp.message = "Login OK";

    // 发送回复
    Send(MsgType::LOGIN_RESP, resp, seq);

    LOG_INFO("AGV %d Logged in.", agvId_);
}

// 心跳处理：收到 Heartbeat -> 更新 WorldManager
    // 1. 轻量级业务：直接在 IO 线程做，不改动
void AgvSession::HandleHbeat(const Heartbeat& msg, int32_t seq) {
    // 未登录拦截
    if(!isLogin_) return;

    if(msg.agvId != agvId_) {
        LOG_WARN("ID mismatch in Heartbeat!");
        return;
    }

    // 更新数字全景
    WorldMgr.OnHeartbeat(msg);
}

// 任务上报：更新进度
void AgvSession::HandleTRepo(const TaskReport& msg, int32_t seq) {
    if(!isLogin_) return;

    // TaskReport 可充当 ACK
    // RPC 处理
    if (msg.refSeq > 0) { // 默认值为0
        HandleAck(msg.refSeq);
    }

    WorldMgr.OnTaskReport(msg); 
    TaskMgr.OnTaskReport(msg); // 含派任务处理

}

// 寻路请求：调用 A* -> 回复 PathResponse
    // 2. 重量级业务：手动扔给线程池
void AgvSession::HandlePRequ(const PathRequest& req, int32_t seq) {
    if(!isLogin_) return;

    /*
    ①.自我保活：工作线程在计算，IO线程有机会在期间析构Session，所以为防止访问被析构后的内存（访问野指针），需要保活
    ②成员函数需要“主语”：
        在 C++ 类中，Send() 本质上是 this->Send() （包括所有内类函数的成员函数调用）
            单线程版：
                在 HandlePRequ 里直接写 Send()，编译器知道是通过当前的 this 指针调用的。
            多线程版 (Lambda)：
                Lambda 脱离了AgvSession类作用域，必须手持一个指针才能调用成员函数）。而既然必须持有一个指针，为了配合第 1 点，我们选择持有 shared_ptr (self) 而不是裸指针 (this).
                Lambda 表达式本质上是一个 匿名的类对象（Functor）。当把 Lambda 扔给线程池时，它就脱离了 AgvSession 的管辖范围，变成了一个独立的“小任务包”。【如果不在 Lambda 的捕获列表 [] 里把 Session 的指针传进去，Lambda 内部是根本看不到 Send 函数的，编译器直接报错。】
    */
    /*
    捕获列表的初始化子句里，只能写「变量名 = 初始化值」，不能在变量名前加任何类型说明符（包括 auto、int、const 等）
    捕获列表的变量类型完全由编译器自动推导，不需要显式指定
    */
   // 【投递到工作线程】
   workerPool_.addtask([self=shared_from_this(), req, seq] () {
        // 求解路径
        auto path = WorldMgr.PlanPath(self->GetId(), req.start, req.end);

        LOG_INFO("[AgvSession] AGV %d Path Planning: (%d,%d) -> (%d,%d), Result: %lu steps",
                 self->GetId(), req.start.x, req.start.y, req.end.x, req.end.y, path.size());

        // 构造回复
        PathResponse resp;
        resp.success = !path.empty();  // 路径为空表示失败
        resp.pathPoints = path;
        resp.failReason = path.empty() ? "Unreachable or already at target" : "";

        // 发送回复
        self->Send(MsgType::PATH_RESP, resp, seq); // 多线程版
   });

    
}



// 任务下发接口  【Worker线程】
bool AgvSession::DispatchTask(const model::TaskRequest& req, RpcCallback cb) {
    // 安全检查：只有登录后才能下发
    if (!isLogin_) return false;
    
    if(!cb) return false;
    
    /*
    // 这个 seq 是 Server 这一侧生成的，用于将来匹配 AGV 回复的 ACK 或 Report
    */
    // 生成属于 Server 的新序列号 以及 待确认构建
    int32_t newSeq = GetNextSeq();

    PendingRequest pr;
    pr.seq = newSeq;
    pr.sendTime = myreactor::Timestamp::now().toMilliseconds();
    pr.cb = cb;

    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        pendingReqs_[newSeq] = pr;
    }

    // 发送     【内部转回 IO线程 】
    Send(MsgType::TASK_REQUEST, req, newSeq);

    return true;
}

// 处理 ACK
void AgvSession::HandleAck(int32_t replySeq) {
    RpcCallback cb = nullptr;

    {
        std::lock_guard<std::mutex> lock(mapMutex_);

        auto it = pendingReqs_.find(replySeq);
        if(it != pendingReqs_.end()) {
            cb = it->second.cb;
            pendingReqs_.erase(it);
        }
    }

    if(cb) {
        LOG_INFO("RPC Match: Seq %d confirmed.", replySeq);
        cb(true, "");
    }
} // // 如果没找到，忽略即可：要么找到true ，要么到超时中 false


void AgvSession::CheckRpcTimeout(int64_t timeoutMils) {
    int64_t now = myreactor::Timestamp::now().toMilliseconds();

    /*vector<pair>和map的选择
        map能够提供键查找(更多的功能)，所以复杂度会高一点
        但这里的场景只需要按序遍历即可，vector足够
    map<seq, cb> 与 vector<pair<seq, cb>>
    */
    using TimeoutItem = std::pair<int32_t, RpcCallback>;
    std::vector<TimeoutItem> timeoutItems;

    {
        std::lock_guard<std::mutex> lock(mapMutex_);

        /*
        范围 for 是 “简洁遍历的语法糖”，设计目标是 “只读 / 修改元素内容”；而代码需要 “遍历中修改容器（删元素）+ 精准控制迭代器”，这超出了范围 for 的能力边界；需要用 显式迭代器遍历
        */
        for(auto it = pendingReqs_.begin(); it != pendingReqs_.end(); ) {
            if(now - it->second.sendTime > timeoutMils) {  //  【超时判断】
                timeoutItems.emplace_back(it->first, it->second.cb);
                it = pendingReqs_.erase(it);
            }else {++it;}
        }
    }

    for(const auto& item : timeoutItems) {
        LOG_WARN("RPC Timeout: Seq %d expired.", item.first);
        item.second(false, "Timeout");
    }

}


void AgvSession::ForceClose() {
    if( auto conn = conn_.lock()) 
        conn->forceClose();
}

}
}