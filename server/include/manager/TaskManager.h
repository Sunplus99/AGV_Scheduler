#pragma once

#include "model/AgvStructs.h"
#include "myreactor/Timestamp.h"
#include "utils/PerfStats.h"  // 性能统计
#include <mutex>
#include <memory>
#include <atomic>
#include <list>
#include <map>
#include <string>
#include <vector>
#include "algo/scheduler/ITScheduler.h"  // 接口

namespace myreactor{
    class ThreadPool;
}

namespace agv{
namespace manager{

// 服务器内部的任务上下文
struct TaskContext {
    // 静态定义：原始需求
    model::TaskRequest req;
    const myreactor::Timestamp createTime; // 原始时间

    // 动态状态
    model::AgvStatus status;
    double progress;
    myreactor::Timestamp updateTime; // 上一次上报的时间
    // myreactor::Timestamp finishTime;

    TaskContext(const model::TaskRequest& r)
        : req(r),
          createTime(myreactor::Timestamp::now()),
          status(model::AgvStatus::IDLE),
          progress(0.0),
          updateTime(myreactor::Timestamp::now())
        //   finishTime(0) // 0 表示未完成
        {}
    
};

class TaskManager {
public: 
    using spTaskContext = std::shared_ptr<TaskContext>;

    static TaskManager& Instance();

    // 必须在 AgvServer 启动时显式调用一次
    void Init(myreactor::ThreadPool* pool);

    // ================= 外部接口 =================

    // ---------- 写操作 ---------- 
    // 发布新任务
    std::string AddTask(model::Point targetPos, model::ActionType targetAct = model::ActionType::NONE);
 
    // 处理任务上报 ： 由 AgvSession 调用
    void OnTaskReport(const model::TaskReport& msg);

    // 外部接口:尝试调度 (通常在有新任务或有车释放时调用)
    void TryDispatch();

    // 设置调度算法 , 用基类指针接收
    void SetScheduler(std::shared_ptr<algo::scheduler::ITScheduler>);

    // 获取性能统计
    const utils::PerfStats& GetTaskStats() const { return taskStats_; }
    const utils::PerfStats& GetScheduleStats() const { return scheduleStats_; }

    // WMS 发完所有任务后调用，告知总任务数，用于判断"全部完成"
    void SetTotalTaskCount(uint64_t total);

private:
    TaskManager();
    ~TaskManager() = default;
    TaskManager(const TaskManager&) = delete;
    TaskManager& operator=(const TaskManager&) = delete;

    // 生成唯一的任务ID
    std::string GenerateTaskId();

    // 执行调度
    void ExecuteDispatch(
        const std::vector<spTaskContext>& tasksSnapst,
        const std::vector<model::AgvInfo>& agvsSnapst,
        std::shared_ptr<algo::scheduler::ITScheduler>);

    // 处理 RPC 发送结果的回调函数 （IO线程调用，加锁）
    void OnDispatchResult(int agvId, const std::string& taskId, bool success, const std::string& reason);

    // 日志打印封装
    // 日志类型枚举：
    enum class LogAction {
        DISPATCH_SUCCESS,       // 派单成功 (INFO)
        DOUBLE_CHECK_FAILED,    // 双重检查失败：小车状态变动（物理与逻辑） (WARN)
        SESSION_LOST,           // 会话丢失：查不到 Session 指针 (WARN)
        DISPATCH_FAILED         // 可能多个原因，没登录/没设置回调 (ERROR)
        // ROLLBACK_FAILED         // 回滚失败 (WARN)
    };

    // 通用日志条目：包含所有类型可能用到的字段
    struct DeferredLog {  // 延迟日志
        LogAction action;       // 类型
        std::string taskId;     // 关联的任务ID
        int agvId;              // 关联的AGV
        int extraVal;           // 额外数值 (距离、状态等)
    };

    void ProcessLogs_TD(const std::vector<DeferredLog>& logs);

private:
    std::mutex mutex_;
    std::atomic<uint64_t> taskSeq_{0};

    // 任务等待队列 
    std::list<spTaskContext> pendingTasks_;

    // 执行中的任务映射 ： AgvId -> TaskContext (1 车 同时 1任务)
    std::map<int, spTaskContext> runningTasks_;

    //持有策略接口指针（基类指针）
    std::shared_ptr<algo::scheduler::ITScheduler> scheduler_;

    // 指针成员可以配合 二段式初始化
    // 初始化为 nullptr，表示"未就绪"
    myreactor::ThreadPool* workerPool_ = nullptr;

    // 性能统计
    utils::PerfStats taskStats_;      // 任务完成统计
    utils::PerfStats scheduleStats_;  // 调度延迟统计

    // 用于判断"全部任务完成"：WMS 发完任务后设置，0 表示未设置
    std::atomic<uint64_t> totalTaskCount_{0};

};


}
}


#define TaskMgr agv::manager::TaskManager::Instance()