#pragma once
#include <functional>
#include <vector>
#include <mutex>
#include <memory> 
#include <thread>
/*
EventLoop 是 Reactor 模型的「心脏」，它的核心思想是通过一个「无限循环」，将「IO 事件等待」「事件分发」「任务处理」串联成一个自动化的驱动流程
EventLoop 现在负责所有调度逻辑、Channel 状态判断，以及与 Epoll 的交互。
*/

/* 大改造：
EventLoop 不仅要能监听 socket fd，还要能监听“别的线程给我的任务”
新增成员变量：
    std::vector<Functor> pendingFunctors_: 一个任务队列（加锁保护），用来存其他线程塞过来的函数。
    int wakeupFd_: 一个 eventfd（Linux 特有系统调用）。

新增方法：
    runInLoop(Functor cb):
        判断：如果是当前 IO 线程调用，直接执行 cb()。
        如果不是当前 IO 线程（比如是工作线程调用的），调用 queueInLoop(cb)。
    queueInLoop(Functor cb):
        加锁，把 cb 放进 pendingFunctors_。
        调用 wakeup()：往 wakeupFd_ 里写 8 个字节。
    wakeup(): 写入 wakeupFd_。
    handleRead() (针对 wakeupFd): 这是一个内部回调。当 wakeupFd_ 可读时，说明有任务来了，把 pendingFunctors_ 里的任务全部取出来执行。
*/
namespace myreactor{

class Epoll;
class Channel;

class EventLoop{
private:
    Epoll* ep_;
    using CB = std::function<void()>;
    CB timercb_;

     // 线程ID与运行状态
    bool quit_;
    const std::thread::id threadId_;
    // id 是定义在 std::thread 类内部的一个嵌套类（或者结构体/类型别名）。

    // 1.任务相关
    using Functor = std::function<void()>;
    std::vector<Functor> pendingFunctors_; //任务队列
    std::mutex mutex_;
    bool callingPendingFunctors_; //表示当前是否正在执行任务队列
    /*if (!isInLoopThread() || callingPendingFunctors_) wakeup();
    意图是：“我在执行当前这批任务时，如果有新任务加进来，请务必把闹钟（wakeupFd）再按响一次，防止我干完手里的活儿直接睡着，把新任务给忘了
    否则 if (!isInLoopThread()) wakeup(); 在这期间的新加任务就不会有通知
    */

    // 2.唤醒相关
    /*
    wakeupChannel 的作用就是操作 events_ (针对 EPOLLIN)，然后通过 EventLoop 实现与 Epoll 的交互。
    */
    int wakeupFd_;  // eventfd
    std::unique_ptr<Channel> wakeupChannel_;

    // 3.定时器机制
    int timerFd_;
    std::unique_ptr<Channel> timerChannel_;
    
    // 内部辅助函数
    static int createEventfd();  // 与 Socket::createNonblocking() 形式统一，都是输出一个fd （static是为了在构造函数中调用，此时没有一个成功创建的对象，那么函数应该直接依赖/属于类 而不是 对象，因此用 static）
    static int createTimerfd();
    static void readTimer(int timerfd);

    /*
    hanldeWakeup():它只是一个门铃,唯一作用是把线程从    epoll_wait 的阻塞沉睡中吵醒.真正的任务不在 handleWakeup 里做，而在 doPendingFunctors 里做,将唤醒与任务分开是因为任务队列里的任务是跨线程塞进来的 std::function，无法直接绑定到 epoll 事件上，只能借用 wakeupFd 来触发执行时机。
    handleTimer():它既是闹钟，也是任务本身。因闹钟与任务不分开是因为定时任务（比如清理连接）通常逻辑比较固定，不需要像任务队列那样作为一个通用的容器。直接在回调里做（或者调用 TcpServer 的回调）路径最短

    保持 handleWakeup 为非静态成员函数:
        语义清晰：handleWakeup 代表的是“这个 EventLoop 对象被唤醒了”。它是对象行为的一部分。
        扩展性：如果未来你想在唤醒时加日志，打印“EventLoop [线程ID] 被唤醒”，或者访问 pendingFunctors_ 的状态，静态函数就做不到了（或者需要传更多参数），而非静态函数随时可以访问 this 的所有数据。
        统一性：它和 handleRead、handleTimer 一样，都是作为对象的回调逻辑存在的。
    readTimerfd 可以是 static:
        它是一个纯粹的“工具函数”或“辅助函数”
        它只依赖传入的参数 timerfd,而不依赖 this 指针
    */
    void handleWakeup(); // 处理 wakeupFd 的读事件
    void handleTimer();
    void doPendingFunctors(); // 处理 wakeupFd 的读事件

    // 【新增】用于接收活跃 Channel 的容器，作为成员变量以复用内存
    std::vector<Channel*> activeChannels_;

public:
    EventLoop();
    ~EventLoop();

    //---核心---- 事件循环
    void run();
    void stop();

    // ---Channel管理---
    // 转发Channel的事件更新请求
    void updatechannel(Channel* ch);
    // 移除Channel的事件
    void removechannel(Channel* ch);

    void settimercb(const CB& fn);

    //----跨线程调用核心----
    // 判断当前是否在 Loop 所在的线程
    bool isInLoopThread() const;
    // 执行任务的统一接口
    void runInLoop(Functor cb);
    // 解释见 Connection::sendInLoop()
    void queueInLoop(Functor cb);

    // 唤醒 ： 像eventfd写数据，从而有读事件
    void wakeup();
};

}