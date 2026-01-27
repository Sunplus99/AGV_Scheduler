#include "algo/scheduler/GreedyScheduler.h"
#include "manager/TaskManager.h"
#include "utils/MathUtils.h"
#include "set"

namespace agv {
namespace algo {
namespace scheduler{

std::vector<DispatchResult> GreedyScheduler::Dispatch(
        const std::vector<std::shared_ptr<manager::TaskContext>>& tasks,
        const std::vector<model::AgvInfo>& candidates)
{
        std::vector<DispatchResult> results;

        // 决策意图记录：记录本批次已经分配的车，防止一车多单 （存在问题 set）
        std::set<int> assignedAgvs;

        for (const auto& task : tasks) {
            int bestAgvId = -1;
            int minDistance = 9999999;

            for (const auto& agv : candidates) {
                // 决策意图检查
                if (assignedAgvs.count(agv.uid)) continue;
                
                // 贪心策略 ： 找最近
                int dis = CalMhtDis(agv.currentPos, task->req.targetPos);
                if (dis< minDistance) {
                    minDistance = dis;
                    bestAgvId = agv.uid;
                }
            }

            // 记录新增决策意图
            if (bestAgvId != -1) {
                results.push_back({task, bestAgvId, minDistance});
            }       
        }

        return results;
}


}
}
}

