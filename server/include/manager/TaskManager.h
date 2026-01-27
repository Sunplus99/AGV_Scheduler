#pragma once

#include "model/AgvStructs.h"
#include "myreactor/Timestamp.h"
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
/*
区别于 model::TaskRequest,其有双重身份
    1.业务源头（The Requirement）; 任务需求的核心提炼就是任务下发的本体 ；被存放在 TaskContext 里，挂在 pendingTasks_ 队列中， 承载业务参数
    2.网络载荷（The Payload）: 服务器把 TaskContext 里存着的那个 TaskRequest信息 作为消息结构体 下发 ：被转换成了 JSON 字符串，塞进了 TCP 二进制流
    发消息的时候可以直接复用TaskContext::TaskRequest
*/
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

    /*单例模式 (Singleton) 与 依赖注入 (Dependency Injection) 的冲突”
    单例模式要求构造函数私有，因此无法在外部像 new TaskManager(pool) 这样传入参数。解决这个问题的标准做法是采用 【二段式初始化 (Two-phase Initialization)】。即：先获取实例，再注入资源  ：【添加 Init 接口】
    */
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
    // 存在于 TaskManager 的内存里，就是一个单纯的数字（1, 2, 3...）。它的唯一作用就是为了防止重复
    // taskID 由 taskSeq_ 和时间戳组合而成
    /* 【业务层求“稳”和“久”，用 uint64 确保哪怕跑一万年 ID 也不重复。】
    业务唯一性：这是用来生成 TaskId (如 T-时间-001) 的。如果系统运行了十年，或者在一秒内生成了海量任务，32 位整数是有可能溢出重复的。而 64 位整数（2^64 ≈ 1.8e19）足以保证人类文明史内不溢出。
    数据库/存储兼容：在生成唯一 ID 方面，通常都会用 64 位整型（比如 Twitter Snowflake 算法也是 64 位），这更符合后端存储的主键设计规范。
    */
    std::atomic<uint64_t> taskSeq_{0};

    // 任务等待队列 
    /*
    场景特点：随机挑选 + 中间删除 (Scheduling)
    调度器（TaskManager）不是简单的“先来后到”，它有复杂的逻辑。
        读操作：需要遍历整个链表，比较哪个任务距离车最近（贪心算法）。
        写操作：找到那个最佳任务后，需要把它从队列的中间或者任意位置拿走（移入 runningTasks）。
    */
    std::list<spTaskContext> pendingTasks_;

    // 执行中的任务映射 ： AgvId -> TaskContext (1 车 同时 1任务)
    std::map<int, spTaskContext> runningTasks_;

    /*
    目前的逻辑看起来 TaskManager 是独占的，但考虑到：
        要支持 运行时切换；
        未来可能接入 Web 监控,打印；
        AI算法对象可能很 重: 想在两个不同的地方复用同一个 AI 策略（例如：一个是真实的调度器，一个是后台跑的“仿真预测器”）。使用 shared_ptr，可以让 TaskManager 和 SimulationManager 共享同一个 AI 模型实例，而不需要在内存里加载两份几百 MB 的模型。

        使用 std::shared_ptr 是最稳健、容错率最高的选择，也是 C++ 后端开发处理“服务组件”时的惯例。
    */
    //持有策略接口指针（基类指针）
    std::shared_ptr<algo::scheduler::ITScheduler> scheduler_;

    // 指针成员可以配合 二段式初始化
    /*
    引用成员：必须在构造函数的初始化列表中立即绑定。
    指针成员：可以在构造函数里，也可以在后续任何时候赋值。
    */
    // 初始化为 nullptr，表示“未就绪”
    myreactor::ThreadPool* workerPool_ = nullptr;

};


}
}


#define TaskMgr agv::manager::TaskManager::Instance()