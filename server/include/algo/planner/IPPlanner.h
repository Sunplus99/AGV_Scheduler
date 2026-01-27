#pragma once
#include "model/AgvStructs.h"
#include "map/GridMap.h"
#include <vector>


namespace agv {
namespace algo {
namespace planner{


class IPPlanner {
public:
    virtual ~IPPlanner() = default;

    // 路径规划接口 : 输入：地图, 起点, 终点; 输出：路径点集
    virtual std::vector<model::Point> Plan(
        const GridMap& map,
        const model::Point& start,
        const model::Point& end
    ) = 0;

    // 获取算法名字,用于日志打印
    virtual std::string Name() const = 0;
};

}
}
}