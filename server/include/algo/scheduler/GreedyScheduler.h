#pragma once
#include "ITScheduler.h"

namespace agv {
namespace algo {
namespace scheduler {

class GreedyScheduler : public ITScheduler {
public:
    GreedyScheduler() = default; 
    ~GreedyScheduler() override = default;

    std::vector<DispatchResult> Dispatch(
        const std::vector<std::shared_ptr<manager::TaskContext>>& tasks,
        const std::vector<model::AgvInfo>& candidates
    ) override;
    /*
    override (以及 virtual, static)：这些关键字只用于头文件 (.h) 中的类声明里。它们是用来告诉编译器类的结构和继承关系的。
    */

    inline std::string Name() const override {
        return "Greedy/NearestNeighbor";
    }

};


}
}
}