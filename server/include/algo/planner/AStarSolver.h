#pragma once
#include "map/GridMap.h"
#include "model/AgvStructs.h"
#include "utils/ObjectPool.h"
#include <vector>
#include <string>


namespace agv{
namespace algo{
namespace planner{

using Point = model::Point;

// 前向声明
struct AStarNode;


class AStarSolver {
public:
    AStarSolver() = default;
    ~AStarSolver();

    // 对外唯一核心接口 ：寻路
    std::vector<Point> FindPath(const GridMap& map, const Point& start, const Point& end);

private:
    // Calculate Heuristic Value : 计算启发式代价,选用曼哈顿距离
    int CalcH(const Point& cur, const Point& end);

    void ReleaseNodes(); // 统一回收本次寻路的节点

private:
    // A*算法内部专用的对象池，管理 AStarNode
    ObjectPool<AStarNode> nodePool_;

    // 记录本次寻路所有申请处的节点指针，
    std::vector<AStarNode*> usedNodes_;

    std::vector<std::vector<int>> visitedTags_;
    int currentSearchId_ = 0;

    // 记录地图尺寸
    int mapWidth_ = 0;
    int mapHeight_ = 0;

};



struct AStarNode {
    int x, y;
    int g, h, f; // f = g + h  
    AStarNode* parent;  // 用于回溯路径

    // 初始化/重置函数 （从对象池里拿出来的内存是脏的）
    void Init(int _x, int _y , int _g , int _h, AStarNode* _p) {
        x = _x; y = _y;
        g = _g; h = _h;
        f = g + h;
        parent = _p;
    }

    struct Compare {  // 仿函数写法，可直接传 类名
        bool operator()(const AStarNode* a, const AStarNode* b) const {
            return a->f > b->f;
        }
    };

};

}
}
}