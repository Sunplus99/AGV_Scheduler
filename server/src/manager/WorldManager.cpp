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

// ---------- 读操作 ----------
std::vector<Point> WorldManager::PlanPath(int agvId, Point start, Point end){
    // 1.检查静态地图
    if (gridMap_.IsObstacle(start.x, start.y)) return {};
    if (gridMap_.IsObstacle(end.x, end.y)) return {};

    // 2.检查动态占用
    //  IsOccupied 内部有读锁，所以这里是线程安全的(能够进入说明没有正在改写)
    if (IsOccupied(start, agvId)) return {}; // 起点快速检查，避免进入后续计算

    // if (IsOccupied(end, agvId)) return {}; // 终点

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
        auto t0 = myreactor::Timestamp::now();
        auto result = currentPlanner->Plan(gridMap_, start, end);
        double planMs = (myreactor::Timestamp::now() - t0) / 1000.0;
        planStats_.recordLatency(planMs);
        return result;
    }

    return {};
}

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