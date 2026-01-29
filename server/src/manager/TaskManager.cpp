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

/*
业务层：`TaskManager::GenerateTaskId` : 给人和数据库看的
目标：生成一个 全局唯一、可追溯、人类可读 的业务凭证。
    为什么加时间戳？（防止重启冲突）
        `TaskManager` 是单例，但程序可能会重启。如果只用 `taskSeq_`（比如从 0 开始），今天发的第 1 个任务叫 `T-1`，明天重启后第 1 个任务还叫 `T-1`。当去查日志或数据库时，就会搞不清这到底是今天的任务还是昨天的任务。
        加上毫秒级时间戳，保证了 跨越进程生命周期 的唯一性。
    为什么是 String？（可读性与灵活性）
        `T-` 前缀让人一眼就能看出这是个“任务 ID”，而不是“AGV ID”或“地图 ID”。
        它通常会出现在 UI 界面、日志文件、数据库主键中。字符串虽然占内存大一点，但在低频的任务调度场景（一秒钟几十个）下，性能损耗可以忽略， 可读性 价值更高。
    原子性需求
        `++taskSeq_` 必须是原子的（或加锁），因为可能有多个线程（如 HTTP 接口线程、ROS 接口线程）同时请求创建任务。
*/

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

        /*
        单例的稳定性 vs 组件的动态性
            WorldManager 作为单例，其自身的内存生命周期贯穿整个进程，是稳定的‘容器’。但它持有的组件（如 planner_）是动态的，允许在运行时被替换（Replaced）
        指针快照（Snapshot）机制
            “我们在执行计算时，利用 shared_ptr 的引用计数机制建立了一个**‘局部快照’。 currentPlanner = planner_; 这一行代码，让局部变量也持有了策略对象的引用（引用计数 +1）。这就相当于给当前的策略对象买了一份‘临时保险’**。”
        生命周期延长（Life Extension）
            “此时，即使其他线程调用 SetPlanner 修改了成员变量 planner_ 的指向（让它指向新算法），旧的算法对象也不会被销毁。 因为我们的局部快照依然持有它。直到当前计算函数结束，局部变量离开作用域，旧对象的引用计数归零，它才会真正析构。这完美实现了无锁且安全的算法热切换。”
        */
        // 在锁内顺便把调度策略指针也拷贝一份，且引用技计数+1，保活到任务当前调度结束
        currentScheduler = scheduler_;
    }

    // 万一还没设置算法
    if(!currentScheduler) return;

    // 3. 【IO 线程】投递任务到工作线程池
    // 捕获 taskInput 和 onlineAgvs (按值拷贝(获取栈对象与跨线程)); 值拷贝调度算法的shared_ptr，引入计数+1
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
            // if (runningTasks_.find(agv.uid)!=runningTasks_.end()) continue;

            candiAgvs.push_back(agv);
        }

        if (candiAgvs.empty()) {
            LOG_WARN("[TaskManager] No candidate AGVs available for dispatch. Total AGVs: %lu", agvsSnapst.size());
            return;
        }

        // 待调度的任务列表 : pendingTasks 里面的全部被存入 快照， 只要在等待队列里的，都需要执行，无需筛选

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
            // 物理状态 
            /*
            状态 (IDLE -> BUSY)：这是 瞬态变化。前 1 毫秒是空闲，后 1 毫秒可能就被另一个线程或者心跳包置为 BUSY。如果不检查，会撞车（逻辑撞车）。必须检查！
            电量 (20.0V -> 19.99V)：这是 缓变物理量;即使掉了一点电，也不会导致逻辑错误（只是车稍微亏电一点去干活了） 【工程取舍】
            */
            auto currentStatus = WorldMgr.GetAgvStatus(agvId);
            if (currentStatus != AgvStatus::IDLE) {
                logs.push_back({LogAction::DOUBLE_CHECK_FAILED, task->req.taskId, agvId, (int)currentStatus});
                continue;
            }
            // 逻辑状态（占用状态）
            if (runningTasks_.count(agvId) > 0) continue;  // 算完做一次总的占用检测

            // 【2. 任务状态检测】
            // 在 无锁的 计算期间，任务的状态发生变化了
            // 任务本身是否被分配
            if (task->req.targetAgvId != -1) continue;
            
            
            // 网络下发
            //定义回调
            /* Lambda 是 C++ 中连接 异步操作 和 上下文保持 的最强胶水。
                    捕获列表，捕获的的就是上下文，而参数列表式就是对外提供的参数表现，lambda函数内部既可以是自己现场定义的，也可以调用别的已经写好的函数
            Lambda 的一个作用是“调整参数列表”，但这只是表象。
            它在底层的核心作用是：“携带上下文（Context Capture）”。
            */
            /*
            Session 定义的回调接口 (RpcCallback)： Session 是底层通用的，它根本不知道什么是 taskId，也不知道现在的 agvId 是多少。它只管通信结果。
                // Session 只提供两个参数：结果好坏、原因
                using RpcCallback = std::function<void(bool success, string reason)>;
            TaskManager 真正需要的处理函数 (OnDispatchResult)： TaskManager 想要回滚任务，它必须知道：是哪辆车、哪个任务失败了。
                // TaskManager 需要四个参数
                void OnDispatchResult(int agvId, string taskId, bool success, string reason);
            */
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
            /*
            一致性保障：runningTasks_[bestAgvId] = task; 这一行就是核心。在网络包到达前，先在内存里占住了坑位。即使网络发送慢了，下一轮循环也不会把这辆车派给别人。
            */
            task->req.targetAgvId = agvId; // 更新 task 状态
            runningTasks_[agvId] = task;
            logs.push_back({LogAction::DISPATCH_SUCCESS, task->req.taskId, agvId, dec.Distance});
            
            hasAssignment = true;
        }

        // ---------------- 批量清理 ----------------
        // pendingTask 是 list , 无键，利用 结点信息 [task 状态是否更新] 判断是否要清除 remove_if
        if (hasAssignment) {
            pendingTasks_.remove_if([](const spTaskContext& t) {
                return t->req.targetAgvId != -1; // 更新过的都要清掉
            });
        }
        
    }

    // 锁外打印日志
    ProcessLogs_TD(logs);

}

/*
std::list::remove_if 是 C++ 双向链表 std::list 的成员函数，用于批量删除链表中所有满足自定义条件的元素，直接在原链表上修改，无需额外内存。
    template <class Predicate>
        void remove_if (Predicate pred);
    参数为可调用谓词， O(N)  ; 谓词 lambda 表达式的 参数为 list中单个元素的代表，保证与list原色类型一致
*/

/*
范围 for 是 “简洁遍历的语法糖”，设计目标是 “只读 / 修改元素内容”；而代码需要 “遍历中修改容器（删元素）+ 精准控制迭代器”，这超出了范围 for 的能力边界；需要用 显式迭代器遍历
*/

/*void TaskManager::TryDispatch() {
    // 【Snapshot : 快照】 : 备选车辆列表
    auto onlineAgvs = WorldMgr.GetAllAgvs();
    if(onlineAgvs.empty()) return;

    // 锁外准备日志容器
    std::vector<DeferredLog> logs;
    logs.reserve(16); // 预估容量,减少扩容开销

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if(pendingTasks_.empty()) return;
        
        // ---------------- 数据准备 ----------------
        // 待调度任务列表 list -> vector ：指针拷贝, O(N)
        std::vector<spTaskContext> taskInput;
        taskInput.reserve(pendingTasks_.size()); // reserve 避免 push_back 时的多次内存重分配
        for (const auto& t : pendingTasks_) taskInput.push_back(t);  

        // 可用的车辆列表 ：从备选列表筛选
        std::vector<AgvInfo> candiAgvs;
        candiAgvs.reserve(onlineAgvs.size());

        for (const auto& agv : onlineAgvs) {
            // 1.必须空闲
            if (agv.status != AgvStatus::IDLE) continue;
            // 2.有电
            if (agv.battery < 20.0) continue;
            // 3.占用检测
            if (runningTasks_.find(agv.uid)!=runningTasks_.end()) continue;

            candiAgvs.push_back(agv);
        }

        if (candiAgvs.empty()) return;

        // ---------------- 核心调度 ----------------
        // 调用调度算法
        auto decisions = scheduler_->Dispatch(taskInput, candiAgvs);

        // ---------------- 执行决策 ----------------
        bool hasAssignment = false; // 【优化标记】, 以便 pendingTasks链表批量清除

        for (const auto& dec : decisions) {
            auto task = dec.task;
            int agvId = dec.agvId;
            
            // 【1.Double Check : 再确认物理状态】
            auto currentStatus = WorldMgr.GetAgvStatus(agvId);
            if (currentStatus != AgvStatus::IDLE) {
                logs.push_back({LogAction::DOUBLE_CHECK_FAILED, task->req.taskId, agvId, (int)currentStatus});
                continue;
            }

            // 工作队列检测
            // GreedyScheduler 内部已经保证了不重复分配(决策意图限制)
            // AI算法内部写复杂的解码逻辑来去重，或者 ：
            // if (runningTasks_.count(agvId) > 0) continue; 
            
            // 网络下发
            auto sess = AgvMgr.GetSession(agvId);
            if (sess == nullptr) { // Session 丢失
                logs.push_back({LogAction::SESSION_LOST, task->req.taskId, agvId, 0});
                continue;
            }
            
            sess->Send(protocol::MsgType::TASK_REQUEST, task->req);

            // 下发成功入队 
            task->req.targetAgvId = agvId; // 更新 task 状态
            runningTasks_[agvId] = task;
            logs.push_back({LogAction::DISPATCH_SUCCESS, task->req.taskId, agvId, dec.Distance});
            
            hasAssignment = true;
        }

        // ---------------- 批量清理 ----------------
        // pendingTask 是 list , 无键，利用 结点信息 [task 状态是否更新] 判断是否要清除 remove_if
--------------------------------------------------------------------------------  
--------------------------------------------------------------------------------
std::list::remove_if 是 C++ 双向链表 std::list 的成员函数，用于批量删除链表中所有满足自定义条件的元素，直接在原链表上修改，无需额外内存。
    template <class Predicate>
        void remove_if (Predicate pred);
    参数为可调用谓词， O(N)  ; 谓词 lambda 表达式的 参数为 list中单个元素的代表，保证与list原色类型一致
--------------------------------------------------------------------------------
--------------------------------------------------------------------------------
        
        if (hasAssignment) {
            pendingTasks_.remove_if([](const spTaskContext& t) {
                return t->req.targetAgvId != -1; // 更新过的都要清掉
            });
        }
        
    }

    // 锁外打印日志
    ProcessLogs_TD(logs);

}*/

/*void TaskManager::TryDispatch() {
    // 【Snapshot : 快照筛选】
    auto onlineAgvs = WorldMgr.GetAllAgvs();

    // 锁外准备日志容器
    std::vector<DeferredLog> logs;
    logs.reserve(16); // 预估容量,减少扩容开销

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if(pendingTasks_.empty()) return;
        if(onlineAgvs.empty()) return;

        // 遍历等待任务
        for (auto it = pendingTasks_.begin(); it != pendingTasks_.end(); ) { // 
            auto task = *it;

            int bestAgvId = -1;

            // ---------- 调度算法 ----------
            int minDistance = 9999999;

            for (const auto& agv : onlineAgvs) {
                // 1.必须空闲
                if (agv.status != AgvStatus::IDLE) continue;
                // 2.有电
                if (agv.battery < 20.0) continue;
                // 3.决策意图检查
                if (runningTasks_.find(agv.uid)!=runningTasks_.end()) continue;

                // 贪心策略 ： 找最近
                int dis = CalMhtDis(agv.currentPos, task->req.targetPos);
                if (dis< minDistance) {
                    minDistance = dis;
                    bestAgvId = agv.uid;
                }
            }

            // ---------- 派单执行 ----------
            if (bestAgvId == -1) { // 当前任务找不到合适的车，看下一个任务
                ++it;
            }
            else{
                // 【1.Double Check : 再确认】
                auto currentStatus = WorldMgr.GetAgvStatus(bestAgvId);
                if (currentStatus != AgvStatus::IDLE) {
                    // LOG_WARN("[TaskManager] Double check failed for AGV %d (Status changed to %d). Skip.", bestAgvId, (int)currentStatus);
                    logs.emplace_back(LogAction::DOUBLE_CHECK_FAILED, task->req.taskId, bestAgvId, (int)currentStatus);

                    ++it;
                    continue;
                }
                // 【2.Reservation : 资源预占】
                // 防止在发包以及车辆响应之前,其他线程/下次循环也不会再考虑这辆车
                task->req.targetAgvId = bestAgvId;
                runningTasks_[bestAgvId] = task;
                
                // 网络下发
                auto sess = AgvMgr.GetSession(bestAgvId);
                if (sess == nullptr) { // Session 丢失
                    // 【3.RollBack : 异常回滚】
                    logs.emplace_back(LogAction::SESSION_LOST, task->req.taskId, bestAgvId, 0);

                    runningTasks_.erase(bestAgvId);
                    ++it;   
                }
                else {
                    sess->Send(protocol::MsgType::TASK_REQUEST, task->req);

                    // LOG_INFO("[TaskManager] Dispatch Task %s to AGV %d (Dist=%d)", task->req.taskId.c_str(), bestAgvId, minDistance);
                    logs.emplace_back(LogAction::DISPATCH_SUCCESS, task->req.taskId, bestAgvId, minDistance);
                    
                    // 成功下发，清理等待队列
                        it = pendingTasks_.erase(it);
                }
            }
        }
    }

    // 锁外打印日志
    ProcessLogs_TD(logs);

}*/


/*
目的是根据 任务汇报消息结构体 来更新 数字任务全景的状态：
    1.找到运行中的任务 
        TaskManager的map的key： 结构体中的agvId 
        if_1:是否找到 ： 找到迭代器并【if_2判断任务序号】，匹配才更新
    2.更新任务数据
        if_3：任务是否完成 ： 完成则 移除
*/
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

        // 有小车空出，下一轮调度
        TryDispatch();
    }
}


}
}
