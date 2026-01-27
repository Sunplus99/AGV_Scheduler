#pragma once

#include <string>
#include <vector>
#include <shared_mutex>
#include "algo/planner/IPPlanner.h"
#include "model/AgvStructs.h"
#include "map/GridMap.h"
#include <memory>

/*
全局世界管理器 (单例模式) - AGV 运行的 “数字孪生世界”
AGV 调度系统的全局核心中枢（单例模式），是连接 “静态地图”、“动态 AGV 状态” 和 “业务逻辑（寻路 / 调度）” 的桥梁
职责：
    管理静态环境（GridMap 地图，障碍物、边界等） 
    管理动态环境（所有在线 AGV 的实时位置、状态）
    对外提供核心服务（路径规划、通行检查），是 TaskManager（任务调度）、AgvSession（AGV 通信）的核心依赖
    解决并发问题（高并发下的读 / 写安全），保证多 AGV 同时请求寻路、更新状态时系统稳定
整个 AGV 系统的 “环境认知” 都集中在 WorldManager 里，所有需要知道 “地图能不能走”“AGV 在哪”“怎么规划路径” 的模块，都要通过它来获取 / 操作数据

“我设计的 WorldManager 是 AGV 调度系统的全局世界管理器，核心定位是‘数字孪生中枢’，采用单例模式保证全局唯一，主要承担三个核心职责：
    资源管理：统一管理静态地图（GridMap）和动态 AGV 状态（在线 / 离线、实时位置），实现‘动静资源分离’；
    核心服务：对外提供线程安全的路径规划（PlanPath）、通行检查（IsWalkable），是 TaskManager 调度任务、AgvSession 处理 AGV 移动的核心依赖；
    并发保障：基于 C++17 的读写锁（shared_mutex）做并发控制，兼顾‘高并发读（寻路 / 检查）’和‘独占写（更新 AGV 状态）’的性能，符合服务器高并发场景的设计需求。
核心亮点：
    单例模式保证全局环境唯一；
    读写锁区分读 / 写操作，提升高并发下的性能；
    线程本地存储（thread_local）的 AStar 实例，避免多线程竞争，复用内存；
    动静资源分离，静态地图只读无锁，动态状态加锁保护，最大化效率。”
*/

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
    /*
    构造 / 析构设为private，外部无法用new Logger()或Logger logger创建实例，只能通过Instance()获取唯一实例。
    */
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

    // 并发控制
    /*shared_mutex ： 读写锁
        agvMutex_.lock_shared();   // 手动加读锁
        agvMutex_.unlock_shared(); // 手动解写锁
        agvMutex_.lock();          // 手动加写锁
        agvMutex_.unlock()         // 手动解写锁
        读锁可获取多把，写锁只能一把
        读锁和写锁之间互斥
        RAII 封装类：
            std::shared_lock<>  // 读锁
            std::unique_lock    // 写锁
    允许多个线程同时 PlanPath/IsWalkable (读)，但 UpdateAgvStatus (写) 会阻塞所有读写
    */
    mutable std::shared_mutex agvMutex_;

    // 算法接口指针
    std::shared_ptr<algo::planner::IPPlanner> planner_;
};

}
}

/*
宏（Macro）没有作用域概念
#define 是预处理指令（Preprocessor），它发生在编译器真正编译代码之前。预处理器就像一个简单的文本替换工具，它完全看不懂 namespace 的大括号 {}。

无论你把 #define 写在 namespace 里面还是外面，对于预处理器来说，它都是一个全局的替换规则。
*/
// 定义宏
#define WorldMgr manager::WorldManager::Instance()