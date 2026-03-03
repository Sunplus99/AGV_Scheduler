#include "manager/TaskManager.h"
#include "manager/WorldManager.h"
#include "session/AgvManager.h"
#include "protocol/MsgType.h"
#include "utils/Logger.h"
#include <sstream>
#include <myreactor/ThreadPool.h>
#include "algo/scheduler/GreedyScheduler.h"  // 默认实现

namespace agv{
namespace manager{

using namespace model;

TaskManager& TaskManager::Instance() {
    static TaskManager instance;
    return instance;
}

TaskManager::TaskManager() // 基类指针指向派生类对象
    : scheduler_(std::make_shared<algo::scheduler::GreedyScheduler>()) {}

void TaskManager::Init(myreactor::ThreadPool* pool) {
    if (workerPool_ != nullptr) {
        LOG_WARN("TaskManager already initialized!");
        return;
    }

    workerPool_ = pool;
    LOG_INFO("TaskManager initialized with ThreadPool.");
}

void TaskManager::SetScheduler(std::shared_ptr<algo::scheduler::ITScheduler> sche) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        scheduler_ = sche;
    }
    LOG_INFO("Scheduler switched to: %s", scheduler_->Name().c_str()); // 多态，调用派生类方法
}

std::string TaskManager::GenerateTaskId() {
    // 格式：T-{毫秒时间戳}-{序号}
    int64_t now = myreactor::Timestamp::now().toMilliseconds();
    std::stringstream ss;
    ss << "T-" << now << "-" << ++taskSeq_;
    return ss.str();  // sstream -> string
}

void TaskManager::OnDispatchResult(int agvId, const std::string& taskId, bool success, const std::string& failreason) {
    if(success) {
        LOG_INFO("[RPC-ACK] Task %s dispatched to AGV %d confirmed.", taskId.c_str(), agvId);
        return;
    }

    LOG_WARN("[RPC-FAIL] Task %s to AGV %d failed: %s. Rolling back...", taskId.c_str(), agvId, failreason.c_str());
    bool rollbackSuccess = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // RollBack
        auto it = runningTasks_.find(agvId);
        // 双重检查：防止 AGV 已经换了别的任务
        if(it != runningTasks_.end() && it->second->req.taskId == taskId) {
            auto task = it->second;

            // 回滚任务状态
            task->req.targetAgvId = -1;  // -1 表示未分配

            // 回滚队列状态 【回到头部】
            pendingTasks_.push_front(task); // 在erase之前，避免迭代器失效
            runningTasks_.erase(it);

            rollbackSuccess = true;
        }
    }


    if(rollbackSuccess){
        LOG_WARN("[RPC-FAIL] Task %s to AGV %d failed. Rollback successful", taskId.c_str(), agvId); 
    }else {
        LOG_ERROR("Rollback failed: Task %s for AGV %d not found or mismatch.", taskId.c_str(), agvId);
    }
}

void TaskManager::SetTotalTaskCount(uint64_t total) {
    totalTaskCount_.store(total, std::memory_order_relaxed);
    LOG_INFO("[TaskManager] Total task count set to %lu.", total);
}

std::string TaskManager::AddTask(Point targetPos, ActionType targetAct) {
    // 1.构造网络包（任务核心提炼）
    TaskRequest req;
    req.taskId = GenerateTaskId();
    req.targetAgvId = -1;  // -1 表示未分配
    req.targetPos = targetPos;
    req.targetAct = targetAct;
    req.priority = 1;

    // 2.包装成上下文
    spTaskContext task = std::make_shared<TaskContext>(req);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 3.入队
        pendingTasks_.push_back(task);
    }

    LOG_INFO("[TaskManager] New Task Added: %s -> Target(%d, %d), CreatedAt: %s",
        req.taskId.c_str(),
        targetPos.x, targetPos.y,
        task->createTime.toFormattedString().c_str()
    );

    // 4. 尝试立即调度(内部含锁)
    TryDispatch(); 

    return req.taskId;
}




void TaskManager::ProcessLogs_TD(const std::vector<DeferredLog>& logs) {
    for (const auto& log : logs) {
        switch (log.action) {
            case LogAction::DISPATCH_SUCCESS:
                LOG_INFO("[TaskManager] Dispatch Success: Task=%s -> AGV=%d (Dist=%d)", log.taskId.c_str(), log.agvId, log.extraVal);
                break;

            case LogAction::DOUBLE_CHECK_FAILED:
                LOG_WARN("[TaskManager] Dispatch Skipped: AGV %d status changed to %d during double check. Task=%s", log.agvId, log.extraVal, log.taskId.c_str());
                break;

            case LogAction::SESSION_LOST:
                LOG_WARN("[TaskManager] Dispatch Failed: Session lost for AGV %d. Rolling back Task=%s", log.agvId, log.taskId.c_str());
                break;

            case LogAction::DISPATCH_FAILED:
            LOG_ERROR("[TaskManager] Dispatch failed for unknown reason: AGV %d , Task=%s", log.agvId, log.taskId.c_str());
                break; 
            default:
                break;
        }
    }
}

void TaskManager::TryDispatch() {
    // 1. 【IO 线程】获取观测世界快照 (读操作，快)
    auto onlineAgvs = WorldMgr.GetAllAgvs();
    if (onlineAgvs.empty()) return;

    // 2. 【IO 线程】加锁获取任务快照 + 策略快照
    // 这里只拷贝指针，速度极快，不会阻塞 IO 很久
    std::vector<spTaskContext> taskInput;
    std::shared_ptr<algo::scheduler::ITScheduler> currentScheduler; 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pendingTasks_.empty()) return;

        taskInput.reserve(pendingTasks_.size());
        for (const auto& t : pendingTasks_) taskInput.push_back(t);

        // 在锁内顺便把调度策略指针也拷贝一份，且引用技计数+1，保活到任务当前调度结束
        currentScheduler = scheduler_;
    }

    // 万一还没设置算法
    if(!currentScheduler) return;

    // 3. 【IO 线程】投递任务到工作线程池
    workerPool_->addtask([this, taskInput, onlineAgvs, currentScheduler]() {
        this->ExecuteDispatch(taskInput, onlineAgvs, currentScheduler);
    });
}

// 【Worker 线程】
void TaskManager::ExecuteDispatch(
    const std::vector<spTaskContext>& tasksSnapst,
    const std::vector<model::AgvInfo>& agvsSnapst,
    std::shared_ptr<algo::scheduler::ITScheduler> currSche) 
{
        // ---------------- 数据准备 ----------------
      
        // 可用的车辆列表 ：从备选列表筛选
        std::vector<AgvInfo> candiAgvs;
        candiAgvs.reserve(agvsSnapst.size());

        for (const auto& agv : agvsSnapst) {
        // 物理状态
            // 1.必须空闲
            if (agv.status != AgvStatus::IDLE) continue;
            // 2.有电
            if (agv.battery < 20.0) continue;
        // 逻辑状态
            // 3.占用检测，无法访问 runningTasks_ (因为没锁)，在后面决策完后再检查的锁内做检查

            candiAgvs.push_back(agv);
        }

        if (candiAgvs.empty()) {
            LOG_WARN("[TaskManager] No candidate AGVs available for dispatch. Total AGVs: %lu", agvsSnapst.size());
            return;
        }


        // ---------------- 核心调度 ----------------
        // 调用调度算法
        LOG_INFO("[TaskManager] Dispatching: %lu tasks, %lu candidate AGVs", tasksSnapst.size(), candiAgvs.size());
        auto decisions = currSche->Dispatch(tasksSnapst, candiAgvs);
        LOG_INFO("[TaskManager] Scheduler returned %lu decisions", decisions.size());

        // ---------------- 执行决策 ----------------
        // 锁前准备
        std::vector<DeferredLog> logs;
        logs.reserve(16); // 预估容量,减少扩容开销
        bool hasAssignment = false; // 【优化标记】, 以便 pendingTasks链表批量清除
        
    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (const auto& dec : decisions) {
            auto task = dec.task;
            int agvId = dec.agvId;

            // 【1.Double Check : 车辆状态检测】
            auto currentStatus = WorldMgr.GetAgvStatus(agvId);
            if (currentStatus != AgvStatus::IDLE) {
                logs.push_back({LogAction::DOUBLE_CHECK_FAILED, task->req.taskId, agvId, (int)currentStatus});
                continue;
            }
            // 逻辑状态（占用状态）
            if (runningTasks_.count(agvId) > 0) continue;  // 算完做一次总的占用检测

            // 【2. 任务状态检测】

            if (task->req.targetAgvId != -1) continue;
            
            
            // 网络下发
            //定义回调
            auto callback = [this, agvId, taskId = task->req.taskId](bool success, const std::string& reason) {
                this->OnDispatchResult(agvId, taskId, success, reason);
            };

            // sess 检查
            auto sess = AgvMgr.GetSession(agvId);
            if (sess == nullptr) { // Session 丢失
                logs.push_back({LogAction::SESSION_LOST, task->req.taskId, agvId, 0});
                continue;
            }

            // sess->Send(protocol::MsgType::TASK_REQUEST, task->req);
            // 调用业务接口
            bool isSend = sess->DispatchTask(task->req, callback);
            if (!isSend) {
                logs.push_back({LogAction::DISPATCH_FAILED, task->req.taskId, agvId, 0});
                continue;
            }

            // 下发成功入队 (Send内部丢给IO线程了)
            task->req.targetAgvId = agvId; // 更新 task 状态
            runningTasks_[agvId] = task;
            logs.push_back({LogAction::DISPATCH_SUCCESS, task->req.taskId, agvId, dec.Distance});

            // 记录调度延迟：从任务创建（AddTask入队）到成功下发的时间
            double schedMs = (myreactor::Timestamp::now().usSinceEpoch()
                              - task->createTime.usSinceEpoch()) / 1000.0;
            scheduleStats_.recordLatency(schedMs);
            
            hasAssignment = true;
        }

        // ---------------- 批量清理 ----------------
        if (hasAssignment) {
            pendingTasks_.remove_if([](const spTaskContext& t) {
                return t->req.targetAgvId != -1; // 更新过的都要清掉
            });
        }
        
    }

    // 锁外打印日志
    ProcessLogs_TD(logs);

}

void TaskManager::OnTaskReport(const TaskReport& msg) {
    // 栈变量 ：临时指针,用于在锁外的接管生命周期) ; 状态标记; 临时数据容器
    bool istask = false;
    bool isTaskFinished = false;
    bool isTaskRejected = false;  // 实现回滚
    double durationSec = 0.0;
    spTaskContext taskToRetry = nullptr;

    myreactor::Timestamp now = myreactor::Timestamp::now();
    

    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = runningTasks_.find(msg.agvId);
        if (it != runningTasks_.end()) {
            spTaskContext taskinmap = it->second;
            if (msg.taskId == taskinmap->req.taskId){ //1
                istask = true; 

                // 任务报错/决绝 -> Rollback
                if (msg.status == AgvStatus::ERROR) { //2
                    isTaskRejected = true;

                    // 恢复任务下派前的状态：
                    taskToRetry = taskinmap; // 等待队列
                    runningTasks_.erase(it);
                }
                // 正常更新/完成
                else { 
                    // 核心更新                       //3
                    taskinmap->status = msg.status;
                    taskinmap->progress = msg.progress;
                    taskinmap->updateTime = now;

                    // 计算耗时
                    durationSec = (now.usSinceEpoch()- taskinmap->createTime.usSinceEpoch())/1000000.0;

                    // 任务是否完成                   //4
                    if(msg.status == AgvStatus::IDLE && msg.progress >= 1.0) {
                        isTaskFinished = true;
                        runningTasks_.erase(it);
                    }
                }    
            }     
        }
    }

    // 1.没找到任务或任务不匹配
    if(!istask) {
        LOG_WARN("[TaskManager] Ignored report from AGV %d: No matching running task.", msg.agvId);
        return;
    }

    // 2. 处理拒绝/失败
    if(isTaskRejected) {
        LOG_WARN("[TaskManager] Task %s REJECTED/FAILED by AGV %d. Rolling back...", msg.taskId.c_str(), msg.agvId);

        if(taskToRetry) {
            // 恢复状态
            taskToRetry->req.targetAgvId = -1;  // -1 表示未分配
            taskToRetry->status = AgvStatus::IDLE;
            taskToRetry->progress = 0.0;

            // 返回等待队列
            std::lock_guard<std::mutex> lock(mutex_);
            pendingTasks_.push_front(taskToRetry);
        }

        // 换车或者唤车
        TryDispatch();
        return;
    }

    // 3.未完成：打印进度
    LOG_INFO("[TaskManager] Task Update: ID=%s, AGV=%d, Progress=%.2f, Elapsed=%.2fs", msg.taskId.c_str(), msg.agvId, msg.progress, durationSec);

    // 4.已完成：打印结算
    if(isTaskFinished){
        LOG_INFO("[TaskManager] Task %s COMPLETED by AGV %d. Total Time: %.2fs", msg.taskId.c_str(), msg.agvId, durationSec);

        // 性能统计：记录任务完成
        taskStats_.incrementCount();
        taskStats_.recordLatency(durationSec * 1000.0);  // 转换为毫秒

        // 每完成 100 个任务，打印一次统计
        if (taskStats_.getCount() % 100 == 0) {
            LOG_INFO("[PERF] Task Statistics: Count=%lu, Avg=%.2fms",
                     taskStats_.getCount(), taskStats_.getAvgLatency());
        }

        // 检查是否所有任务都完成了（WMS 已设置总数 且 完成数 == 总数）
        uint64_t total = totalTaskCount_.load(std::memory_order_relaxed);
        if (total > 0 && taskStats_.getCount() >= total) {
            LOG_INFO("[PERF] ========== All Tasks Completed! Performance Report ==========");
            scheduleStats_.printStats("Scheduling Latency");
            taskStats_.printStats("Task Completion");
            WorldMgr.GetPlanStats().printStats("Path Planning Latency");
            LOG_INFO("[PERF] =============================================================");
        }

        // 有小车空出，下一轮调度
        TryDispatch();
    }
}


}
}
