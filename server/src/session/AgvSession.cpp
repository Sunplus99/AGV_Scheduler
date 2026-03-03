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
    // 自我状态检查 (防同连接重复) 
    if(isLogin_) {
        LOG_WARN("Duplicate Login Request from AGV %d", agvId_);
        return;
    }

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

    // 全局状态检查 (顶号策略 / 踢旧连接) 
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
void AgvSession::HandlePRequ(const PathRequest& req, int32_t seq) {
    if(!isLogin_) return;

   // 【投递到工作线程】
   workerPool_.addtask([self=shared_from_this(), req, seq] () {
        // 记录开始时间
        auto startTime = myreactor::Timestamp::now();

        // 求解路径
        auto path = WorldMgr.PlanPath(self->GetId(), req.start, req.end);

        // 计算路径规划延迟（微秒转毫秒）
        auto endTime = myreactor::Timestamp::now();
        double planningLatencyMs = (endTime - startTime) / 1000.0;

        LOG_INFO("[AgvSession] AGV %d Path Planning: (%d,%d) -> (%d,%d), Result: %lu steps, Latency: %.2fms",
                 self->GetId(), req.start.x, req.start.y, req.end.x, req.end.y, path.size(), planningLatencyMs);

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

    using TimeoutItem = std::pair<int32_t, RpcCallback>;
    std::vector<TimeoutItem> timeoutItems;

    {
        std::lock_guard<std::mutex> lock(mapMutex_);

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