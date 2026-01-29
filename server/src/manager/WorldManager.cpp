#include "manager/WorldManager.h"
#include "utils/Logger.h"
#include "algo/planner/AStarPlanner.h"
#include "myreactor/Timestamp.h" 

namespace agv{
namespace manager{


WorldManager& WorldManager::Instance() {
    static WorldManager instance;
    return instance;
}

WorldManager::WorldManager() 
    : planner_(std::make_shared<algo::planner::AStarPlanner>())
{}

void WorldManager::SetPlanner(std::shared_ptr<algo::planner::IPPlanner> plan) {
    {
        std::unique_lock<std::shared_mutex> lock(agvMutex_); 
        planner_ = plan;
    }    
    LOG_INFO("Path Planner switched to: %s", planner_->Name().c_str());
}

// 模式 1: 文件加载
bool WorldManager::Init(const std::string& mapPath) {
    LOG_INFO("Initializing World from file: %s ...", mapPath.c_str());

    // 尝试从文件加载
    if (!gridMap_.LoadMap(mapPath)) {
        LOG_ERROR("Failed to load map from %s", mapPath.c_str());
        return false;
    }
    
    if (gridMap_.GetWidth() <= 20 && gridMap_.GetHeight() <= 20) {
        gridMap_.PrintMap();
    } else {
        LOG_INFO("Map is too large to print in console.");
    }
    return true;
}

// 模式 2: 默认地图 
bool WorldManager::Init() {
    LOG_INFO("Initializing World with Default Map...");
    gridMap_.CreateDefaultMap();
    
    gridMap_.PrintMap();
    return true;
}

// 模式 3: 随机地图 
bool WorldManager::Init(int w, int h, double obstacleRatio) {
    LOG_INFO("Initializing World with Random Map [%dx%d, ratio=%.2f]...", w, h, obstacleRatio);
    gridMap_.CreateRandomMap(w, h, obstacleRatio);
    
    // 大地图就别 PrintMap 了，屏幕会炸，或者只打印尺寸信息
    if (w <= 20 && h <= 20) {
        gridMap_.PrintMap();
    } else {
        LOG_INFO("Map is too large to print in console.");
    }
    return true;
}

/*
起点检查完，锁释放了，状态变了怎么办？
    这是一个经典的 TOCTOU (Time Of Check To Time Of Use) 竞态条件问题;计算出的路径在生成的瞬间，起点其实已经撞车了.
为什么我们不把锁加在整个 PlanPath ? 后果：
    系统卡死：A* 是 CPU 密集型计算（耗时操作）。如果在这里持有锁，意味着在计算路径的这几十毫秒内，所有其他 AGV 都无法上报位置（写锁被阻塞），所有其他线程的查询也会被阻塞
    吞吐量暴跌：如果有 100 台车，它们只能排队一个接一个地算路，系统瞬间瘫痪。
工业界如何解决？（乐观并发 + 层次化控制）
    通常接受这个“瞬间的不一致性”，并通过后续手段弥补：
    策略 A：分层检查 (Global vs Local)
        Global Planner (WorldManager): 负责“宏观战略”。它的检查只是为了防止“显而易见的错误”（比如起点在墙里，或者起点当前明显被堵死）。即便由于竞态条件漏掉了一瞬间的变化，也无伤大雅。
        Local Planner / Executor (AGV/TaskManager): 负责“微观战术”。当 TaskManager 把路径下发给 AGV 后，AGV 在真正执行每一步移动前，必须再次检查前方是否有障碍（通过车载雷达或申请资源锁）。
    策略 B：任务下发时的二次校验 (Double Check)
        PlanPath 只负责算路（耗时，不加锁）。
        TaskManager 在拿到路径准备下发给 AGV 的那一刻（耗时极短），再次加锁快速检查一下路径的前几个点是否被占用。如果被占用，触发重算。
PlanPath 里的检查是为了Fail Fast（快速失败），【避免明显无效的计算浪费 CPU】，而不是为了保证绝对的原子性安全。
    这正是做架构设计时需要权衡的 Consistency (一致性) vs Performance (性能)。在路径规划环节，我们通常倒向 Performance。
*/
// ---------- 读操作 ----------
std::vector<Point> WorldManager::PlanPath(int agvId, Point start, Point end){
    // 1.检查静态地图
    if (gridMap_.IsObstacle(start.x, start.y)) return {};
    if (gridMap_.IsObstacle(end.x, end.y)) return {};

    // 2.检查动态占用
    //  IsOccupied 内部有读锁，所以这里是线程安全的(能够进入说明没有正在改写)
    if (IsOccupied(start, agvId)) return {}; // 起点快速检查，避免进入后续计算

    // if (IsOccupied(end, agvId)) return {}; // 终点

    /*
    单例的稳定性 vs 组件的动态性
        WorldManager 作为单例，其自身的内存生命周期贯穿整个进程，是稳定的‘容器’。但它持有的组件（如 planner_）是动态的，允许在运行时被替换（Replaced）
    指针快照（Snapshot）机制
        “我们在执行计算时，利用 shared_ptr 的引用计数机制建立了一个**‘局部快照’。 currentPlanner = planner_; 这一行代码，让局部变量也持有了策略对象的引用（引用计数 +1）。这就相当于给当前的策略对象买了一份‘临时保险’**。”
    生命周期延长（Life Extension）
        “此时，即使其他线程调用 SetPlanner 修改了成员变量 planner_ 的指向（让它指向新算法），旧的算法对象也不会被销毁。 因为我们的局部快照依然持有它。直到当前计算函数结束，局部变量离开作用域，旧对象的引用计数归零，它才会真正析构。这完美实现了无锁且安全的算法热切换。”
    */

    // 获取当前【策略的 快照】
    // 使用 shared_lock (读锁) 保护 planner_ 指针的读取
    std::shared_ptr<algo::planner::IPPlanner> currentPlanner;
    {
        std::shared_lock<std::shared_mutex> lock(agvMutex_); 
        currentPlanner = planner_; // 引用计数+1，保证在函数执行期间对象不被销毁
    }
    // 3.执行算法
    // 安全检查：防止 planner_ 未初始化
    if (currentPlanner) {
        // 这里调用的是接口的 Plan，具体是用 A* 还是 Dijkstra，由 currentPlanner 的实际类型决定
        return currentPlanner->Plan(gridMap_, start, end);
    }
    
    return {};
}

/*
读操作（PlanPath/IsWalkable）占 99%，写操作（更新 AGV 状态）占 1%
如果用普通 mutex：100 个线程同时请求寻路，只能排队加锁，性能极低；
用 shared_mutex（读写锁）：多个读线程可以同时加读锁，只有写线程来的时候才阻塞，完美适配 “多读少写” 的场景。
*/
// 检查动态车辆
bool WorldManager::IsOccupied(int x, int y, int selfId) const {
    // 检查某处是否有车辆，为了避免脏堵、读，需要加读锁
    std::shared_lock<std::shared_mutex> lock(agvMutex_);

    for(auto& [id, info] : onlineAgvs_) {
        if (id==selfId) continue; // 忽略自己

        // 重叠检查
        if (info.currentPos.x==x && info.currentPos.y==y)
            return true;
    }
    return false;
}

bool WorldManager::IsOccupied(Point point, int selfId) const {
    return IsOccupied(point.x, point.y, selfId);
}

model::AgvStatus WorldManager::GetAgvStatus(int agvId) const {
    
        std::shared_lock<std::shared_mutex> lock(agvMutex_);
        auto it = onlineAgvs_.find(agvId);
        if (it != onlineAgvs_.end()) {
            return it->second.status;
        }
        // 下线了
        return model::AgvStatus::UNKNOWN;
    
}

std::vector<Info> WorldManager::GetAllAgvs() const {
    std::vector<Info> res;
    {
        std::shared_lock<std::shared_mutex> lock(agvMutex_);
        res.reserve(onlineAgvs_.size());
        for(const auto& pair : onlineAgvs_)
            res.push_back(pair.second);
    }
    return res;
}

// ---------- 写操作 ----------
// 1. 登录：填充静态身份信息 + 初始化
void WorldManager::OnAgvLogin(const model::LoginRequest& req) {
    Info info;
    // --- 静态身份信息
    info.uid = req.agvId;
    info.version = req.version;
    // --- 初始化状态（从登录请求中获取）
    info.status = model::AgvStatus::IDLE;
    info.battery = 100.0;
    info.currentPos = req.initialPos;  // 使用客户端提供的初始位置
    // --- 运维保活信息
    info.lastHeartbeatTime = myreactor::Timestamp::now().toMilliseconds();

    { // 细化写锁作用域
        std::unique_lock<std::shared_mutex> lock(agvMutex_); //写锁
        onlineAgvs_[info.uid] = info;
    }
    // 释放锁之后再打印日志，避免 IO 操作阻塞其他线程
    LOG_INFO("[WorldManager] AGV %d Logged in at (%d, %d) with status=%d, battery=%.1f",
             info.uid, info.currentPos.x, info.currentPos.y, (int)info.status, info.battery);
}

/* “锁内极速计算，锁外从容打印”
“准备数据 -> 极速更新 -> 延后处理” 模式
*/
// 2. 心跳：主要更新物理属性,顺带状态
void WorldManager::OnHeartbeat(const model::Heartbeat& msg) {
    // 1. 准备数据
    // 系统调用放在锁外，减少临界区时间
    int64_t now = myreactor::Timestamp::now().toMilliseconds();
    bool isUnkownAgv = false;

    {
        std::unique_lock<std::shared_mutex> lock(agvMutex_); // 写锁

        auto it = onlineAgvs_.find(msg.agvId);
        if (it != onlineAgvs_.end()) {
            // --- 动态物理信息
            it->second.currentPos = msg.currentPos;
            it->second.battery = msg.battery;
            // --- 逻辑状态信息
            it->second.status = msg.status;
            // --- 运维保活信息
            it->second.lastHeartbeatTime = now;
        } else {
            // 策略：收到未知车辆心跳，打印警告
            // LOG_WARN("Heartbeat from unknown AGV: %d", msg.agvId);
            // 改成 记录标记
            isUnkownAgv = true;
        }
    }

    if(isUnkownAgv)
        LOG_WARN("Heartbeat from unknown AGV: %d", msg.agvId);
    
}

// 3. 任务上报：主要更新逻辑属性，顺带位置
void WorldManager::OnTaskReport(const model::TaskReport& msg) {
    // 1. 准备数据
    // 系统调用放在锁外，减少临界区时间
    int64_t now = myreactor::Timestamp::now().toMilliseconds();

    {
        std::unique_lock<std::shared_mutex> lock(agvMutex_); // 写锁

        auto it = onlineAgvs_.find(msg.agvId);
        if (it != onlineAgvs_.end()) {
            // ---逻辑状态信息
            it->second.status = msg.status;
            it->second.currentTaskId = msg.taskId;
            it->second.taskProgress = msg.progress;
            // ---动态物理信息
            it->second.currentPos = msg.currentPos;
            // --- 运维保活信息
            it->second.lastHeartbeatTime = now;
        }
    }
    
}

// AGV 下线
void WorldManager::OnAgvLogout(int agvId) {
    {
        std::unique_lock<std::shared_mutex> lock(agvMutex_);
        onlineAgvs_.erase(agvId);
    }
    LOG_INFO("[WorldManager] AGV %d Logged out.", agvId);
}



}
}