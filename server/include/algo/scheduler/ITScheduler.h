#pragma once
#include <vector>
#include <string>
#include <memory>
#include "model/AgvStructs.h"


// 若已包含命名空间的头文件，可直接写 ，但为了避免循环引用，不包含头文件, 选择前置声明
// struct agv::manager::TaskContext;

namespace agv {
    namespace manager {   // 打开命名空间
        struct TaskContext; // 声明
    }
}

namespace agv {
namespace algo {
namespace scheduler{


    
// 调度结果: 谁 去干 哪个任务
struct DispatchResult {
    std::shared_ptr<manager::TaskContext> task;
    int agvId;
    int Distance;   // 用于日志
};

class ITScheduler {  // 抽象类,仅提供一种接口规范
public:
    virtual ~ITScheduler() = default;

    // 任务调度接口 : 输入：待调度的任务列表, 可用的车辆列表; 输出：调度解落列表
    virtual std::vector<DispatchResult> Dispatch(
        const std::vector<std::shared_ptr<manager::TaskContext>>& tasks,
        const std::vector<model::AgvInfo>& candidates
    ) = 0; // 纯虚函数

    // 获取算法名字,用于日志打印
    virtual std::string Name() const = 0;

};



}
}
}