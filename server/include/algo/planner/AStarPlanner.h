#include "IPPlanner.h"
#include "AStarSolver.h" // 引用真正的计算类

namespace agv{
namespace algo{
namespace planner{

class AStarPlanner : public IPPlanner {
public:
    std::vector<model::Point> Plan(const GridMap& map, const model::Point& start, const model::Point& end) override {

        static thread_local AStarSolver solver; 
        
        return solver.FindPath(map, start, end);
    }

    inline std::string Name() const override { return "A* (TLS Optimized)"; } 
};


}
}
}