#pragma once
#include "ITScheduler.h"

namespace agv {
namespace algo {
namespace scheduler{

class LyaSACScheduler : public ITScheduler {
    /*
    LyaSACScheduler() {
        // TODO: Init Python Interpreter or Load ONNX Model
        // printf("Loading SAC Policy Network...\n");
    }*/

    std::vector<DispatchResult> Dispatch(
        const std::vector<std::shared_ptr<manager::TaskContext>>& tasks,
        const std::vector<model::AgvInfo>& candidates
    ) override {
        std::vector<DispatchResult> results;
        
        // ...

        return results;
    }

    inline std::string Name() const override {
        return "Lyapunov-SAC-RL";
    }
};

}
}
}