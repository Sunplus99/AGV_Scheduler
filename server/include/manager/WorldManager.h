#pragma once

#include <string>
#include <vector>
#include <shared_mutex>
#include "algo/planner/IPPlanner.h"
#include "model/AgvStructs.h"
#include "map/GridMap.h"
#include "utils/PerfStats.h"
#include <memory>


namespace agv{
namespace manager{

using Point = model::Point;
using Info  = model::AgvInfo;

class WorldManager{
public:
    // 创建与获取单例实例
    static WorldManager& Instance();

    // ================= 加载静态地图 =================
    // 模式 1: 从文件加载 (生产环境/特定关卡)
    bool Init(const std::string& mapPath);

    // 模式 2: 使用默认 10x10 地图 (调试/单元测试)
    bool Init(); 

    // 模式 3: 生成随机大地图 (性能压测)
    bool Init(int w, int h, double obstacleRatio);

    // =================== 核心业务 ===================
    // ---------- 读操作 ----------
    // 路径规划
    std::vector<Point> PlanPath(int agvId, Point start, Point end);

    // 检查动态车辆占用
    bool IsOccupied(int x, int y, int selfId) const;
    bool IsOccupied(Point point, int selfId) const;

    // 获取地图
    const GridMap& GetGridMap() const {return gridMap_;}

    // 获取单车状态
    model::AgvStatus  GetAgvStatus(int agvId) const;

    // 获取所有车辆快照
    std::vector<Info> GetAllAgvs() const;

    // ---------- 写操作 ---------- 按消息类型分类 : 由 AgvSession 调用
    
    // 1. 处理登录 (初始化静态信息 + 初始状态)
    void OnAgvLogin(const model::LoginRequest& req);

    // 2. 处理心跳 (更新物理信息：位置、电量、状态)
    void OnHeartbeat(const model::Heartbeat& msg);

    // 3. 处理任务上报 (更新逻辑信息：任务ID、进度、位置、状态)
    void OnTaskReport(const model::TaskReport& msg);

    // 4. 处理下线
    void OnAgvLogout(int agvId);


    // 切换算法
    void SetPlanner (std::shared_ptr<algo::planner::IPPlanner> plan);
    

private:

    WorldManager(); // 默认使用 A*
    ~WorldManager() = default;

    /*
    禁止拷贝 : 防止用户误写 WorldManager  worldmanager =  WorldManager::Instance()，导致创建新实例，破坏单例特性。
    */
    WorldManager(const WorldManager&) = delete;
    WorldManager& operator=(const WorldManager&) = delete;
private:
    // 静态环境资源
    GridMap gridMap_;

    // 动态环境资源
    std::map<int, Info> onlineAgvs_;

    mutable std::shared_mutex agvMutex_;

    // 算法接口指针
    std::shared_ptr<algo::planner::IPPlanner> planner_;

    // 路径规划延迟统计
    utils::PerfStats planStats_;

public:
    const utils::PerfStats& GetPlanStats() const { return planStats_; }
};

}
}

// 定义宏
#define WorldMgr manager::WorldManager::Instance()