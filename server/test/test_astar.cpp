// tests/test_astar.cpp
#include "algo/AStar.h"
#include "map/GridMap.h"
#include <iostream>

int main() {
    GridMap map;
    map.CreateDefaultMap(); // 10x10, 四周是墙

    agv::algo::AStar astar;
    auto path = astar.FindPath(map, {1,1}, {8,8});

    std::cout << "Path size: " << path.size() << std::endl;
    for(auto& p : path) {
        std::cout << "(" << p.x << "," << p.y << ") -> ";
    }
    std::cout << "END" << std::endl;
    return 0;
}