#include "algo/planner/AStarSolver.h"
#include "utils/Logger.h"
#include <algorithm>
#include <limits>
#include <queue>

namespace agv{
namespace algo{
namespace planner{

struct Offset {int x , y;};
static const Offset dirs[4] = {{0,-1},{1,0},{0,1},{-1,0}}; // 顺时针 ： 上 右 下 左

// AStar::AStar() {
//     // 编译器在这里生成构造代码，初始化 nodePool_
//     // 此时它知道 sizeof(AStarNode)，一切正常！
// }

AStarSolver::~AStarSolver() {
    ReleaseNodes();
}

int AStarSolver::CalcH(const Point& cur, const Point& end) {
    return std::abs(cur.x - end.x) + std::abs(cur.y - end.y);
}

void AStarSolver::ReleaseNodes() {
    for(auto* node : usedNodes_) 
        nodePool_.destroy(node);

    usedNodes_.clear();
}


/*
无权图，广度优先版
*/
std::vector<Point> AStarSolver::FindPath(const GridMap& map, const Point& start, const Point& end) {
    // 基础检查,对静态地图的检查 ： 防御性 (不通过 Manager 直接调用算法)
    if (map.IsObstacle(start) || map.IsObstacle(end)){
        LOG_WARN("AStar: Start or End is obstacle.");
        return {};
    }

    if (start == end) return {};

    // 1.初始化
    ReleaseNodes();

    if (map.GetWidth()!=mapWidth_ || map.GetHeight()!=mapHeight_) { // 初始化或者新地图
        mapWidth_ = map.GetWidth();
        mapHeight_ = map.GetHeight();
        visitedTags_.assign(mapHeight_, std::vector<int>(mapWidth_, 0));
        currentSearchId_ = 0; // 地图变了，Tag 重置
    }

    // 防溢出
    if (currentSearchId_ == std::numeric_limits<int>::max()) {
        currentSearchId_ = 0;
        for(auto& row : visitedTags_)
            std::fill(row.begin(), row.end(), 0);
    }

    ++currentSearchId_;

    // 起点初始化与入队
    std::priority_queue<AStarNode*,std::vector<AStarNode*>, AStarNode::Compare> pq;

    AStarNode* startNode = nodePool_.construct();
    startNode->Init(start.x, start.y, 0, CalcH(start, end), nullptr);
    usedNodes_.push_back(startNode); // 记录指针
    pq.push(startNode);              // 入队
    visitedTags_[start.y][start.x] = currentSearchId_; 

    // 3.搜索循环
    AStarNode* targetNode = nullptr;
    while(!pq.empty()){
        AStarNode* cur = pq.top();
        pq.pop();

        if(cur->x==end.x && cur->y==end.y){
            targetNode = cur;
            break;  // 到达目标点
        }

        for(const auto& [dx, dy] : dirs) {
            int next_x = cur->x + dx;
            int next_y = cur->y + dy;

            if(map.IsObstacle(next_x, next_y) || visitedTags_[next_y][next_x]==currentSearchId_) continue;

            AStarNode* next = nodePool_.construct();
            next->Init(next_x, next_y, cur->g + 1, CalcH({next_x,next_y}, end), cur);

            usedNodes_.push_back(next);
            pq.push(next);
            visitedTags_[next_y][next_x] = currentSearchId_;
        }
    }

    std::vector<Point> path;
    if(targetNode) { // 到达目标点
        AStarNode* cur = targetNode;
        while (cur != nullptr){
            path.push_back({cur->x,cur->y});
            cur = cur->parent;
        }
        std::reverse(path.begin(),path.end());
    }

    ReleaseNodes();
    return path;
}



}
}
}