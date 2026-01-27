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


/*
A * 算法是带启发式的最短路径算法，核心公式：f = g + h
    g：起点到当前节点的实际代价（和 Dijkstra 的 “累计代价” 完全一致）；
    h：当前节点到终点的预估代价（A * 独有的启发式，Dijkstra 中 h=0）；
逻辑：A优先扩展f最小的节点（Dijkstra 优先扩展g最小的节点）；h 的存在让 A“朝着终点方向搜索”，比 Dijkstra 少扩展大量无关节点，效率更高。

*/
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

    /*
    传统visited数组每次寻路要遍历重置（O (W*H)），Tag 方式只需自增 ID，检查visitedTags_[y][x] == currentSearchId_即可判断是否访问（O (1)），大地图下效率提升显著
    */
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

    /*
     1.确定优先逻辑 ： f 越小越优先
     2.定堆信号：越小越优先 -> 小根堆 ， 
     3.写函数：小根堆比较器是 greater >
        手写一个结构体比较函数 ，要求为返回值与形式是 xx > yy*/
    struct Compare {  // 仿函数写法，可直接传 类名
        bool operator()(const AStarNode* a, const AStarNode* b) const {
            return a->f > b->f;
        }
    };

};

}
}
}