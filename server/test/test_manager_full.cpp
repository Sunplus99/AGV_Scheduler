#include "manager/WorldManager.h"
#include <iostream>
#include <cassert>

using namespace agv::manager;
using namespace agv::model;

int main() {
    auto& world = WorldManager::GetInstance();
    
    // 1. 初始化
    world.Init(""); 

    // 2. 模拟 AGV 1 上线 (停在 2,2)
    AgvInfo agv1;
    agv1.id = 1;
    agv1.currentPos = {2, 2};
    world.OnAgvLogin(agv1);

    // 3. 模拟 AGV 2 上线 (停在 8,8)
    AgvInfo agv2;
    agv2.id = 2;
    agv2.currentPos = {8, 8};
    world.OnAgvLogin(agv2);

    // 4. 测试 IsWalkable
    // (2,2) 被 AGV 1 占了，应该不可走
    if (!world.IsWalkable(2, 2, 999)) {
        std::cout << "[PASS] (2,2) is blocked by AGV 1." << std::endl;
    } else {
        std::cerr << "[FAIL] (2,2) should be blocked!" << std::endl;
    }

    // 5. 测试 AGV 2 寻路 (从 8,8 到 1,1)
    // 既然 2,2 被占了，如果路径穿过 2,2，A* 应该会自动绕开吗？
    // 注意：目前的 IsWalkable 是瞬时状态。A* 寻路时，start/end 会检查，但中间过程是只查静态地图的。
    // 如果要实现动态避障，需要在 A* 的 neighbor 扩展里回调 WorldManager::IsWalkable。
    // 但目前你的 A* 只依赖 GridMap，这是为了性能。
    // 我们先测试基础寻路：
    auto path = world.PlanPath(2, {8, 8}, {1, 1});
    std::cout << "Path found, steps: " << path.size() << std::endl;

    assert(path.size() > 0);

    return 0;
}