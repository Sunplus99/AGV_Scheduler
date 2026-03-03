#include "session/AgvManager.h"
#include "manager/WorldManager.h"
#include "utils/Logger.h"
#include <string>
#include "myreactor/ThreadPool.h"

namespace agv {
namespace session{

AgvManager& AgvManager::Instance() {
    static AgvManager instance;
    return instance;
}

// ========== 网络层 -> 业务层 ==========
// 连接建立 : 创建匿名 Session, 建立物理映射
void AgvManager::OnNewConn(const spConnection& conn, myreactor::ThreadPool& pool) {    
    // 【乐观分配】
    auto newsess = std::make_shared<AgvSession>(conn, pool);

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 防止两个线程同时处理同一个 conn (虽然理论上 Reactor 模型不会发生)
        if (connMap_.find(conn) == connMap_.end()) { // 【锁内检查】
            connMap_[conn] = newsess;

            // 返向绑定：让 Connection 直接 持有 Session
            conn->setContext(newsess);

        } else {
            // 极其罕见的重复，直接返回，newSession 离开作用域自动析构
            return; 
        }
    }
    // 【锁外日志】
    LOG_INFO("New Connection [%s:%d], Session Created.", conn->ip().c_str(), conn->port());
}

// 断开连接 ：销毁 Session,清理映射,触发业务下线
void AgvManager::OnClose(const spConnection& conn) {
    // 栈变量：定义一个临时指针，用于在锁外接管生命周期 ; 定义一个临时 id，用于标记是否需要清除idMap，以及在锁外接管
    spSession sess = nullptr; 
    int agvId = -1; // 记录ID，无论是否抢占，都记录下来打印日志
    bool needLogout = false; // 标记是否需要 业务下线

    {   // 临界区：只做 临界资源 Map 的查找
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = connMap_.find(conn);   // 1.查找物理连接 ：conn -> session
        if (it != connMap_.end()) {
            sess = it->second; // 接管  

            if (sess->IsLogin()) {   // 只有已登录的才涉及 idMap 清理
                agvId = sess->GetId();
                auto idIt = idMap_.find(agvId);  // 2.查找逻辑连接 : id -> session
                
                //【关键判断】: 两个session比较，只有相等，意味着没有新的session抢占，则 Agv真正下线，清理 idMap
                if(idIt!=idMap_.end() && sess==idIt->second) {
                    idMap_.erase(idIt);   // 【清 idMap】
                    // WorldMgr.OnAgvLogout(id);  X
                    // WorldMgr.OnAgvLogout内部有锁，可能发生死锁，尽量释放锁后再拿另外一把锁
                    needLogout = true;
                    
                }// else 有新的，保留 idMap
            } 

            // 清理要删的session，以及connection
            // connMap_.erase(conn);  // 【清 connMap】  又查了一次红黑树
            connMap_.erase(it); //拿到迭代器了，直接删迭代器，是O(1),效率更高
        }
    }

    if(sess) {
        if(needLogout) {
            WorldMgr.OnAgvLogout(agvId);
            LOG_INFO("AGV %d Logged out (Active Disconnect).", agvId);
        } else if (agvId != -1) {
            LOG_WARN("AGV %d Old Session Closed (Preempted by new login). No cleanup needed.", agvId);
        } else { 
            LOG_INFO("Unlogin Session Disconnected.");
        }      
    }
}

// "Init Point/Container -> Lock -> Copy Pointer/Swap Container  -> Unlock -> Do Work"

// ========== 业务层 -> 网络层 ==========
// 强制下线

void AgvManager::KickAgv(int agvId) {
    // 定义一个临时指针，用于在锁外接管生命周期
    spSession sess = nullptr;

    {   // 临界区：只做 临界资源 Map 的查找
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = idMap_.find(agvId);
        if(it != idMap_.end()) sess = it->second;
    }

    if(sess != nullptr) {
        LOG_WARN("Kicking AGV %d by Administrator/System...", agvId);
        sess->ForceClose();
    } else {
        LOG_WARN("Kick failed: AGV %d not found.", agvId);
    }
}

// ========== 查找接口
// 收到网络包 -> 查 Session -> 处理业务
AgvManager::spSession AgvManager::GetSession(const spConnection& conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connMap_.find(conn);
    if (it==connMap_.end()) return nullptr;
    return it->second;
}

// 业务id -> 查 Session 
AgvManager::spSession AgvManager::GetSession(int agvId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = idMap_.find(agvId);
    if(it==idMap_.end()) return nullptr;
    return it->second;
}

    
// 身份登记到业务身份表 : 建立 逻辑映射
void AgvManager::RegisterAgvId(int agvId, spSession sess){
    bool isneedreplace = false;
    spSession oldsess = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
    
        // 先检查  id-旧session 对
        auto it = idMap_.find(agvId);
        if(it != idMap_.end()){ 
            isneedreplace = true;
            oldsess = it->second;
        }

        // 更新替换/插入
        idMap_[agvId] = sess;
    }

    if(isneedreplace)
        LOG_WARN("Race Condition or Overwrite: AGV %d replacing session %p with %p", agvId, oldsess.get(), sess.get());

    LOG_INFO("AGV ID %d Registered.", agvId);
    
}


void AgvManager::CheckAllTimeouts(int64_t timeoutMils) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& pair : connMap_) {
        auto session = pair.second;
        if (session) {
            // 3. 调用 Session 自己的检查函数
            session->CheckRpcTimeout(timeoutMils);
        }
    }
}

}
}