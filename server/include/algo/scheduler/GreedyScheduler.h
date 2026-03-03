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

    inline std::string Name() const override {
        return "Greedy/NearestNeighbor";
    }

};


}
}
}