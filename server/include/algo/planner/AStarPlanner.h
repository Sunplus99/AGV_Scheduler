#include "IPPlanner.h"
#include "AStarSolver.h" // 引用真正的计算类

namespace agv{
namespace algo{
namespace planner{

class AStarPlanner : public IPPlanner {
public:
    std::vector<model::Point> Plan(const GridMap& map, const model::Point& start, const model::Point& end) override {
        // static: 保证 solver 只初始化一次
        // thread_local: 保证每个线程有自己独立的 solver 副本，互不干扰，无需加锁
        /*
        thread_local：表示 “每个线程拥有一份独立的 solver 实例”，生命周期和线程一致；
            避免多线程共享 AStar 实例（AStar 内部有对象池、Tag 数组，共享会导致线程安全问题）；
        static：保证每个线程的 solver 只初始化一次（第一次调用 PlanPath 时创建，后续复用）。
            复用内存：每个线程的 solver 只需初始化一次，后续寻路复用对象池，避免频繁创建 / 销毁 AStar，提升性能；
            对比 “每次寻路 new AStar”：无内存开销，对比 “全局 AStar 加锁”：无锁竞争，是高并发下的最优解；
        */
        static thread_local AStarSolver solver; 
        
        return solver.FindPath(map, start, end);
    }

    // Thread Local Storage Optimized（线程局部存储优化）
    inline std::string Name() const override { return "A* (TLS Optimized)"; } 
};


}
}
}