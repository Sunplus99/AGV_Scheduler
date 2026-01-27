#pragma once

#include "protocol/AgvCodec.h"
#include "model/AgvStructs.h"
#include "myreactor/Connection.h"
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>



/*
AgvSession 是 “单个 TCP 连接 + 单个 AGV 业务逻辑” 的封装体 : 每个 AGV 客户端的 TCP 连接，对应一个 AgvSession 实例。
核心使命是：把底层网络连接（Connection）和上层 AGV 业务逻辑绑定(借用Connection)，处理该 AGV 的所有消息收发和业务处理
*/
namespace myreactor{
    class ThreadPool;
}

namespace agv{
namespace session{

/*
只要一个类的生命周期是被 std::shared_ptr 管理的（AgvSession 就在 AgvManager 的 map 里被 shared_ptr 管理），且它需要在自己的成员函数内部，把自己作为 shared_ptr 传递给别人时，就必须继承 enable_shared_from_this。
*/
class AgvSession : public std::enable_shared_from_this<AgvSession> {
public:
    using spConnection = std::shared_ptr<myreactor::Connection>;

    using RpcCallback = std::function<void(bool success, const std::string& reason)>;                                     // 【RPC 增强接口】

    /*
    在 C++ 规范（如 Google C++ Style Guide）中，所有单参数的构造函数，除非你真的非常确定需要隐式转换（比如 BigInt 从 int 转换），否则一律必须加 explicit。
        对于 AgvSession(spConnection conn)：
            我们不希望 Connection 被自动视为 AgvSession。
            我们希望创建 Session 是一个显式、郑重的行为。
    explicit AgvSession(spConnection conn);
    */
    AgvSession(spConnection conn, myreactor::ThreadPool& pool);
    ~AgvSession();

    // ============================ 外部业务接口  ============================
    // ---------------------------------------------------------
    // “请求 - 响应”模式 (Request - Response)
    // ---------------------------------------------------------
    // 下行消息（Response）是包含在上行处理函数（Handle）内部
    // 业务处理接口 : 由 Dispatcher回调 
    /*userFunc(conn, obj, msg.head.seq)
    在服务器开发中，“业务处理”通常 = “计算 + 响应”以及序列号:
            【计算】必须要源数据，即obj, 而obj在不同时期表现为msg和obj，
            【响应】依靠 conn, conn的本质: “回信地址”。
            【序列号】即 msg.head.seq
        因此 UserFunc 的 参数列表必需的就是以上 3 个 
    */
   /* seq:
   AGV主动 : “原样奉还” (Echo), 目的是让 AGV 知道 Server 回复的是哪一次请求
   AGV被动 : “引用确认” (Reference), 目的是让 Server 知道 AGV 收到了哪条指令
   */
    void HandleLogin(const model::LoginRequest& req, int32_t seq); // AGV主动
    void HandleHbeat(const model::Heartbeat& msg, int32_t seq);    // AGV主动
    void HandleTRepo(const model::TaskReport& msg, int32_t seq);   // AGV被动
    void HandlePRequ(const model::PathRequest& req, int32_t seq);  // AGV主动

    // ---------------------------------------------------------
    // “服务器主动推送”模式 (Server Push) + RPC 支持
    // ---------------------------------------------------------
    // Server 主动发起，主动下发                    【RPC 增强接口】
    // 下行指令接口，供 TaskManager 调用 ：外部只需传入纯粹的业务结构体，无需关心 Seq 和网络细节
    bool DispatchTask(const model::TaskRequest& req, RpcCallback cb = nullptr);

    // ---------------------------------------------------------
    // 状态查询与控制
    // ---------------------------------------------------------
    // 状态查询
    int GetId() const {return agvId_;}
    bool IsLogin() const {return isLogin_;}
    // 强制下线
    void ForceClose();

    // 超时检查 : AgvManager调用，并一起绑定给TcpServer          【RPC 增强接口】
    void CheckRpcTimeout(int64_t timeoutMils = 5000);  // 5000ms = 5s

private:
    // 内部核心工具 ： 借用 Connection 发送; 只服务于内部的 HandleXxx 和 DispatchXxx
    // 分装 weak.lock() 与 Codec的一体化接口 send,避免每处都要做 weak.lock()的操作
    template<typename T>
    void Send(protocol::MsgType type, const T& obj, int32_t seq = 0) {
            spConnection conn = conn_.lock();
            if (conn) 
                codec::send(conn, type, obj, seq);
    }

    /*
    通信层：`AgvSession::GetNextSeq`
    目标：生成一个 轻量级、高效、协议兼容的 通信标记。
        为什么是 `int32_t`？（协议标准与带宽）
            这是为了塞进二进制的网络包头（Header）里。
            网络传输讲究寸土寸金，一个 `int32` 只占 4 字节。如果传一个 `T-167888...` 这样的字符串，可能要占 20+ 字节，对于高频的心跳包来说是巨大的浪费。
        TCP/IP 协议层的序列号也是整数，这是通信界的标准做法。
        为什么允许回绕（Overflow）？（生命周期短）
            Session 的 Seq 只需要保证在 【短时间内不重复即可】（用于匹配 Request 和 Response）。比如发了 Seq=100 的请求，只要在几秒内回来的 Response 也是 Seq=100，就能匹配上。
            即使 long run 之后 Seq 溢出变成了负数或者 0，只要不和几秒前的包重复，完全不影响逻辑。它不需要像 TaskID 那样“千秋万代”地唯一。
        原子性需求
            必须使用 `std::atomic`，因为 Worker 线程（下发任务）和 IO 线程（可能也在发某些控制指令）可能会并发调用 `DispatchTask`，导致 Seq 竞争。
    */

    /*
    fetch_add 是 C++ 标准库中 std::atomic<T> 原子类型的成员函数，属于原子操作的核心接口，专门用于在多线程环境下安全地对原子变量执行「加法 + 返回旧值」的原子操作
    return nextSeq.fetch_add(1) + 1; 和 return ++nextSeq; 返回结果完全一致，因为：
        ++nextSeq 是语法糖 内部执行 fetch_add(1) + 1 并返回 ；效果为返回新值
        nextSeq++ 是语法糖 内部执行 fetch_add(1) 并返回 ； 效果为返回旧值
        nextSeq += 2 是语法糖 内部执行 fetch_add(2) + 2 并返回 ；效果为返回新值
        fetch_add(1) + 1 : 
            fetch_add(1):完全原子，整个 “读取旧值 → 加 1 → 写入新值” 的过程不可分割，多线程下无竞态,返回值是旧值
            + 1： 不是原子操作，但这是「单线程内的普通整数加法」（基于 fetch_add 返回的旧值计算），不涉及对共享变量 nextSeq 的修改，因此不会引入线程安全问题。
    为什么选择 fetch_add？
        如果需求只是「原子加 x 并返回新值」，++nextSeq/ += x 确实更简洁，但 fetch_add 有个关键优势：
            显式内存序：在性能敏感的场景下，可以指定更弱的内存序（如 std::memory_order_relaxed）来减少同步开销，而 ++ 通常使用默认的强内存序，无法灵活调整。
    */
    // 生成下一个序列号
    inline int32_t GetNextSeq() {
        // return nextSeq.fetch_add(1) + 1;
        return ++nextSeq;
    }

    // 处理 RPC 确认, 在 TaskReport 内部调用，TaskReport也含 Ack 的作用
    void HandleAck(int32_t replyseq);               // 【RPC 增强接口】

private:
    /*
    AgvSession 不拥有 Connection 的生命周期。Connection 的生杀大权必须完全掌握在 TcpServer 手里
    weak_ptr 的作用：“借用”
        我知道这个连接可能随时会断。发消息的时候，我先看一眼它还在不在（lock()）。如果还在，我就发；如果不在（已经析构了），那我就不发了。
        如果不用weak_ptr,未来拖展到 Connection持有Session的强引用，会形成循环计数，不能析构
    */
    std::weak_ptr<myreactor::Connection> conn_;

    int agvId_ = -1;
    bool isLogin_ = false;

    // 发送序列号技术其
    // 只需保证单调递增
    /* 【通信层求“快”和“省”，用 int32 足够，允许回绕。】
    协议标准与带宽节省：在网络协议头中，每多一个字节都是成本。4 字节（32位）是标准的 Sequence Number 长度（如 TCP 协议头也是 32 位）。如果你改成 64 位，每个包头就大 4 字节，对于高频小包（如心跳）是极大的浪费。
    允许回绕 (Wrap-around)：它的作用仅仅是匹配最近几秒内的“请求-响应”对。只要你在几秒内发的包不超过 42 亿个（2^32），即使数字溢出变成负数或者归零，也不会和之前的包混淆。所以，它不需要无限增长，哪怕是负数也没关系。
    */
    /*
    std::atomic<int32_t> nextSeq_{0}; 并不是 “初始化列表（std::initializer_list）” 语法，而是 C++11 引入的统一初始化语法（Uniform Initialization）（也叫 “大括号初始化”）；它的核心作用是安全、统一地初始化 std::atomic 原子对象，既符合现代 C++ 语法规范，又能避免传统初始化方式的坑。
    */
    std::atomic<int32_t> nextSeq{0};


    /* 待确认请求表 与 待确认请求的记录结构
    【异步通信中的三个核心难题：匹配（Matching）、时效（Timing）和 上下文（Context）】
        seq 是唯一索引键，但在结构体里冗余存一份通常是为了方便调试打印，或者在某些需要遍历 vector 而不是 map 的场景下能快速知道这条记录属于谁。
        
        时间戳：防止请求“由于意外”无限期占用资源，触发超时重试机制。
            发出请求后，可能（网线被铲断了，包丢了。AGV 死机了，没发回 ACK。AGV 逻辑卡死，处理了 1 分钟还没完）各种原因，如果没有时间戳，会导致这条请求永远在请求表内（内存里），久而久之，内存泄漏（Memory Leak），且 Server 会一直以为 AGV 还在忙，导致任务卡死。因此结合存一个时间戳配合超时处理机制

        上下文/记忆体：记住“当初发送时的目的”和“收到结果后的处理逻辑”，连接过去与未来
            DispatchTask 函数发送完数据就 立刻返回（Return）了。 当函数返回那一刻，所有的局部变量（taskId 是什么，agvId 是多少，Task 对象指针在哪）全部从栈上销毁了。 等到收到ACK，已不知道要做什么（发给谁的？失败了要回滚哪个任务？）。
            而callback 保存了逻辑现场，即捕获上下文（Lambda 在创建时，把 this 指针、taskId、agvId 全部拷贝/移动到了这个对象内部），还保存逻辑（调 OnDispatchResult）
    */
    struct PendingRequest {                      // 【RPC 增强接口】
        int32_t seq;
        int64_t sendTime;
        RpcCallback cb; 
    };

    std::map<int32_t, PendingRequest> pendingReqs_;   // 【RPC 增强接口】

    /* 待确认请求表 
        Key: 序列号, Value: 请求上下文
    会被 Worker线程（AgvSession::DispatchTask内）Send以前）
        IO线程 （AgvSession::HandleTRepo -> AgvSession::HandleAck） 使用，
        因此需要 加锁
    */
   std::mutex mapMutex_;                               // 【RPC 增强接口】

    /* 选择引用成员而不是指针成员
    引用必须绑定到一个真实存在的对象:“必须有线程池才能工作” 的强烈语义;
        引用成员：必须在构造函数的初始化列表中立即绑定。
        指针成员：可以在构造函数里，也可以在后续任何时候赋值。
    */ 
    myreactor::ThreadPool& workerPool_;

};

}
}