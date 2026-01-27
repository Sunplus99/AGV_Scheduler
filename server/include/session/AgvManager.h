#pragma once
#include "session/AgvSession.h"
#include "myreactor/Connection.h"
#include <memory>
#include <map>

/*
通过维护 “TCP 连接 ↔ AgvSession”“AGV ID ↔ AgvSession” 双映射关系，统一管理所有 AGV 会话的创建、销毁、查找，保证网络连接事件（创建 / 关闭）与业务会话状态（登录 / 下线）的一致性，同时为其他模块（Dispatcher/TaskManager）提供安全、统一的会话查找接口。
AgvManager 确实是桥梁，但它更重要的作用是 “翻译” 和 “同步”。
    【翻译】：把底层的 Connection 翻译成业务的 AgvSession（给 Dispatcher 用），把业务的 AgvId 翻译成 AgvSession（给 TaskManager 用）。
    【同步】：确保 TCP 断了的时候，业务状态也能跟着断；业务想断的时候，TCP 也能跟着断。
如果没有它，服务器就是“精神分裂”的：网络层连接断了，业务层还显示在线；业务层发了任务，网络层不知道往哪个 Socket 发。

核心职责：
    1. 维护双向映射：[Connection -> Session] (物理层) 和 [AgvId -> Session] (业务层)
    2. 生命周期桥梁：将底层的网络事件 (Connect/Close) 转化为业务层的会话事件 (Login/Logout)
    3. 线程安全中心：为多线程的 IO 回调和业务调用提供统一的加锁保护
*/

namespace myreactor{
    class ThreadPool;
}

namespace agv{
namespace session{

class AgvManager{
public:
    using spConnection = std::shared_ptr<myreactor::Connection>;
    using spSession = std::shared_ptr<AgvSession>;

    static AgvManager& Instance();

    // ========== 网络层 -> 业务层 ==========
    /* 
    由 TcpServer 在 IO 线程回调:
        newconncb_(conn);
        closecb_(conn);
    将网络连接的生灭同步给Session
    */
    // 连接建立 : 创建匿名 Session, 建立物理映射
    void OnNewConn(const spConnection& conn, myreactor::ThreadPool& pool);

    // 断开连接 ：销毁 Session,清理映射,触发业务下线
    void OnClose(const spConnection& conn);

    // ========== 业务层 -> 网络层 ==========
    /*
    由业务逻辑调用，主动控制网络连接
    ： 创建连接不需要反向同步，业务层凭空想出一个 Connection 是没有意义的
    */
    // 强制下线 ：管理员踢人或者安全拦截
    /* Loopback 设计
    只触发关闭连接，不直接删除 Session ;  让 CloseCallback 回调 OnClose() 统一收尾，避免资源竞争
    */
    /*
    架构设计深度解析】：为什么采用 "Loopback (回环)" 方式踢人？
    即：在 KickAgv 中只调用 forceClose，不删除 Session；让 OnClose 回调统一负责清理。
    1. [核心必须] 并发一致性与数据防丢 (Concurrency & Data Integrity)
        - 场景：踢人指令发出后，Close 任务正在 IO 线程排队，但 socket 缓冲区刚好涌入最后一个关键包（如 "任务完成"）。
        - 风险：若在 KickAgv 立即删除 Session（先斩后奏），IO 线程收到数据时查不到 Session，导致关键数据被丢弃。
        - 解决：回环方案保证 Session 存活至 TCP 彻底断开的最后一刻，确保能处理 "临终遗言"，维持业务状态一致性。
    2. [架构优化] 单一真理来源 (Single Source of Truth / DRY)
        - 痛点：若在 KickAgv 和 OnClose 各写一套清理逻辑（通知 WorldManager、落库、日志），一旦业务变更（如新增下线埋点），极易漏改其中一处，导致 "被踢" 和 "意外断线" 的处理行为不一致，引发隐蔽 Bug。
        - 解决：让 OnClose 成为 Session 死亡的唯一判定点和清理点，逻辑高度内聚。
    3. [审计优势] 完整的生命周期追踪 (Auditability)
        - 痛点：若先删 Session，OnClose 触发时仅剩裸露的 Connection 指针，无法通过 map 反查出是 "哪辆车" 断开了。
        - 解决：回环方案在 OnClose 执行前保留 Session，确保能记录完整的下线日志（如 ID、在线时长、断开原因）。
    */
    void KickAgv(int agvId);

    // ========== 查找接口

    // 收到网络包 -> 查 Session -> 处理业务
    spSession GetSession(const spConnection& conn);

    // 业务处理完成 -> 查 Session -> 发送
    spSession GetSession(int agvId);

    
    // 身份绑定 : 建立 逻辑映射
    void RegisterAgvId(int agvId, spSession sess);
    
    // 供 TcpServer 定时调用的检查接口,内含 每个session的heckRpcTimeout
    void CheckAllTimeouts(int64_t timeoutMils = 5000);


private:
    AgvManager() = default;
    ~AgvManager() = default;
    
    /*
    禁止拷贝 : 防止用户误写 auto  worldmanager =  AgvManager::Instance()，导致创建新实例，破坏单例特性。
    */
    AgvManager(const AgvManager&) = delete;
    AgvManager& operator=(const AgvManager&) = delete;

private:
    std::mutex mutex_;

    // 物理映射 Connection -> Session
    // 持有 Session 的强引用 : 保证连接在, Session就在
    /* Connection强引用
    【防止“意外死亡”】：
        如果没有 connMap_ 持有强引用，那么 Connection 的生命周期完全由底层的 TcpServer (或者 Reactor 的 event loop) 控制。 场景：万一底层逻辑有 Bug，或者某个瞬间所有引用都丢了，Connection 就会析构。 而 AgvManager 持有一份强引用，相当于由 业务层 给 Connection 上了一道 “最后保险”。 Connection 只有在 AgvManager::OnClose 里执行 connMap_.erase 之后，才能真正去死。 这保证了业务清理逻辑（Session 析构、通知 WorldManager）一定发生在 Connection 析构 之前。
        这也是“回环设计”能生效的基础——必须有人拉住 Connection 不让它死，直到走完 OnClose 流程
    【技术限制：std::map 的 Key 必须要“稳”】
        如果用 weak_ptr 做 Key：
            std::weak_ptr 本身没有重载 < 运算符，不能直接作为 std::map 的 Key。虽然可以通std::owner_less 来实现比较，但使用起来非常麻烦。
        如果用 Connection* (裸指针) 做 Key：
            虽然可以用，但裸指针不参与引用计数。
            存在 ABA 问题（虽然在 64 位系统和内存池下概率极低，但逻辑上不严谨）：旧对象析构了，新对象恰好分配到了同一个内存地址。你的 map 可能会错误地把新连接当成旧连接。
        使用 shared_ptr 做 Key：
            shared_ptr 内置了比较机制（基于控制块地址），非常适合做 Key。它保证了 “只要这个 Key 在 map 里，这个 Key 指向的对象就一定活着”。这让代码逻辑变得非常简单的：不需要在使用 map 时判空或检查 expired。
    */ 
    std::map<spConnection, spSession> connMap_;

    // 逻辑映射 AgvId -> Session
    // 提供 O(logN) 的业务查找能力
    std::map<int, spSession> idMap_;


};


}
}

// 定义宏
#define AgvMgr agv::session::AgvManager::Instance()