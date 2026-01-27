#include <myreactor/EventLoop.h>
#include <myreactor/Epoll.h>
#include <myreactor/Channel.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

using namespace myreactor;
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
你之前在 Socket 和 Epoll 中使用静态工厂方法 (createEpoll, createNonblocking) 是为了解决 资源创建（系统调用）和 对象封装 的职责分离问题。

Socket/Epoll: 资源创建依赖于操作系统 API（socket(), epoll_create()），可能失败并返回 -1。静态工厂方法负责处理这个系统调用和错误。

EventLoop: EventLoop 构造函数只需要创建 C++ 对象 (Epoll*)。这个操作是纯粹的内存分配 (new)，不会因为外部环境而失败，因此，将创建逻辑放在初始化列表中是标准的 C++ RAII 做法。
*/
/*
EFD_CLOEXEC 当进程调用 exec() 系列函数（如 execve()、execl() 等）加载并执行一个新程序时，带有 FD_CLOEXEC 属性的文件描述符会被自动关闭，不会被继承到新启动的程序进程中；反之，未设置该属性的文件描述符会被新进程继承，可能造成不必要的资源泄露、文件描述符耗尽或意外的 IO 操作
*/
int EventLoop::createEventfd(){
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(evtfd < 0){
        perror("eventfd create error");
        exit(1);
    }
    return evtfd;
}

int EventLoop::createTimerfd(){
    // CLOCK_MONOTONIC : MONOTONIC 单调递增的、持续向前的
    // TFD_NONBLOCK | TFD_CLOEXEC: 非阻塞，exec时关闭
    int tfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if(tfd < 0){
        perror("timerfd create error");
        exit(1);
    }
    return tfd;
}

void EventLoop::readTimer(int timerfd){
    uint64_t one = 1;
    ssize_t n = ::read(timerfd, &one, sizeof(one));
    if(n != sizeof(one))
        perror("EventLoop::readTimer() reads mistach");
}

/*
1.new Epoll：
new 操作符先在堆内存中为 Epoll 对象分配空间；
调用 Epoll 的无参构造函数，初始化这个堆上的 Epoll 对象；
返回该堆对象的内存地址（Epoll* 类型指针）。
2.ep_(new Epoll)：将 new Epoll 返回的指针赋值给 EventLoop 的成员变量 ep_（指针赋值）。
*/
EventLoop::EventLoop()
    : ep_(new Epoll),
      quit_(false),
      threadId_(std::this_thread::get_id()),
      callingPendingFunctors_(false),
      wakeupFd_(createEventfd()),
      wakeupChannel_(std::make_unique<Channel>(this, wakeupFd_)),
      timerFd_(-1), //初始化为 -1，表示未启动
      timerChannel_(nullptr)
{
    wakeupChannel_->setreadcallback(std::bind(&EventLoop::handleWakeup,this));
    wakeupChannel_->enablereading();

    // timerChannel_->setreadcallback(std::bind(&EventLoop::handleTimer,this));
    // timerChannel_->enablereading();

    // // 启动定时器：每隔5秒触发一次
    // // 一切准备就绪（回调绑好了、Epoll 挂好了），最后才启动计时。这是最自然的逻辑
    // struct itimerspec howlong;
    // memset(&howlong, 0 , sizeof(howlong));
    // howlong.it_value.tv_sec = 5; // 初始/第一次
    // howlong.it_value.tv_nsec = 0;
    // howlong.it_interval.tv_sec = 5; // 之后间隔
    // howlong.it_interval.tv_nsec = 0;

    // ::timerfd_settime(timerFd_, 0 , &howlong, NULL);
} 


EventLoop::~EventLoop() {
    ::close(wakeupFd_);
    if (timerFd_ != -1) ::close(timerFd_);
    delete ep_;
}
/*
1.调用 ep_ 指向的那个堆上 Epoll 对象的析构函数（Epoll::~Epoll()）；
2.释放该 Epoll 对象占用的堆内存。
*/


/*
doPendingFunctors() 不是“只有 wakeup 唤醒时才执行”，而是**“只要 Loop 醒着，每次循环最后都检查一遍”**。

这样做保证了：没有任何任务会被遗漏，且尽可能利用 CPU 缓存和活跃状态，减少延迟。
情况一：纯粹的跨线程任务唤醒:专门为了任务而醒，执行了任务
情况二：
    纯粹的普通 Socket IO 唤醒,此时任务队列（pendingFunctors_）通常是空的。耗时：加锁 -> 检查 vector 为空 -> 解锁 -> 返回。这个过程极快（纳秒级），对性能几乎无影响。虽然没任务做，但检查一下也不亏
情况三：混合情况
    正在处理IO事件的时候添加发送任务，此时 queueInLoop 仅加入任务而不唤醒（
        if (!isInLoopThread() || callingPendingFunctors_) {
            wakeup();
        }）
    ，当IO事件处理完后依旧会执行 doPendingFunctors() ，可以理解为：省去了一次“再次陷入 epoll_wait -> 再次被 wakeup 唤醒”的系统调用开销。这个任务“搭了 IO 事件的便车”，被更快地处理了。
*/

void EventLoop::run(){
    // “防御性编程”（Defensive Programming）
    if(threadId_ != std::this_thread::get_id()) {
        printf("EventLoop::run() in wrong thread!\n");
        exit(1);
    }

    while(!quit_){
        // 清空 activeChannels_，复用内存
        activeChannels_.clear();

        // 好的设计应该由 Epoll 负责填充一个 std::vector<Channel*>，EventLoop 只拿结果，不关心底层是用 epoll 还是 poll
        ep_->poll(10*1000,&activeChannels_);

        // 3. EventLoop 遍历并调度 Channel
        // if (activeChannels_.empty()){
        //     if(timeoutcb_) timeoutcb_(this);
        // } else{
        //     for(auto ch : activeChannels_) 
        //         ch->handleEvent();
        // }
        // 这里的超时时间(10s)其实无所谓了，因为 timerfd 每 5s 会把 poll 唤醒一次
        for(auto ch : activeChannels_)
            ch->handleEvent();

        // 5. 处理跨线程任务
        doPendingFunctors();
    }

    
}


void EventLoop::stop(){
    if(quit_) return; // 幂等性检查：如果已经停了，直接返回
    
    quit_ = true;
    /*
    主线程想要退出时，会触发 RAII 链式析构：~EventLoopThreadPool 销毁 threads_ 容器，进而触发 ~EventLoopThread。
    ---主线程想要退出时，先自动调用 ~EventLoopThreadPool（unique_ptr管理），销毁threads_成员的时候会自动调用~EvenLoopThread（unique_ptr管理）---
    在主线程析构了 EventLoopThread 对象而 IO线程还在跑的情况下，可能会访问已经被释放的内存，因此在C++标准中，如果 std::thread 对象在析构时仍然处于 joinable 状态，程序直接调用 std::terminate()，整个进程异常退出。
    为了避免以上情况，我们在~EventLoopThread内使用 if(thread_.joinable()) thread_.join(); 来保证先回收 IO线程，释放 EventLoopThread对象，再关闭主线程。
    而退出IO线程需要：①退出事件循环，因此给run()中的while加一个标志条件quit_ ②唤醒IO线程（否则会阻塞在epoll_wait处，等待被唤醒，影响即可关闭）
    将以上两点放在 join()前，可全部封装到一个函数EventLoop::stop()内
    */
    if(!isInLoopThread()){
        wakeup();
    }
    
}

void EventLoop::updatechannel(Channel *ch) {
    int op = ch->inepoll() ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;  // Eventlopp依据ch的状态信息，决定op选项，并传递给epoll

    ep_->update(op,ch); // 传递 Channel 指针和决定的操作
    ch->setinepoll(true); // 更新 Channel状态

}

void EventLoop::removechannel(Channel *ch){
    if(ch->inepoll()){
        ep_->update(EPOLL_CTL_DEL,ch); // 同样调用 update，只是 op 不同
        ch->setinepoll(false);
    }
}

// 懒加载：只有被调用 set 时才启动硬件定时器
void EventLoop::settimercb(const CB& fn){
    timercb_ = fn;

    if(timerFd_ == -1){
        timerFd_ = createTimerfd();
        timerChannel_ = std::make_unique<Channel>(this, timerFd_);
        timerChannel_->setreadcallback(std::bind(&EventLoop::handleTimer,this));
        timerChannel_->enablereading();

        // 启动定时器：每隔5秒触发一次
        // 一切准备就绪（回调绑好了、Epoll 挂好了），最后才启动计时。这是最自然的逻辑
        struct itimerspec howlong;
        memset(&howlong, 0 , sizeof(howlong));
        howlong.it_value.tv_sec = 1; // 初始/第一次
        howlong.it_value.tv_nsec = 0;
        howlong.it_interval.tv_sec = 1; // 之后间隔
        howlong.it_interval.tv_nsec = 0;

        int ret = ::timerfd_settime(timerFd_, 0 , &howlong, NULL);
        if(ret == -1){
            perror("timerfd_settime failed");
        }
    }
}

// ---- 线程安全相关----

bool EventLoop::isInLoopThread() const {
    return threadId_ == std::this_thread::get_id();
}

void EventLoop::wakeup() {
    uint64_t one = 1;
    // 必须读 8 个字节，读出来的 n 也是 8
    ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
    if(n != sizeof(one))
        perror("EventLoop::wakeup() writes mismatch");
}

void EventLoop::handleWakeup() {
    uint64_t one = 1;
    ssize_t n = ::read(wakeupFd_, &one, sizeof(one));
    if(n != sizeof(one))
        perror("EventLoop::handleWakeup() reads mismatch");
}

/*
第一版：
这正是“机制与策略分离”（Mechanism vs. Policy）在代码实现上的完美体现。设计思路是这样的：
    统一的类模板（机制）：所有的 EventLoop（无论是 Main 还是 IO）都拥有处理定时器的能力（都有 timerFd_，都有 handleTimer 函数）。这是为了代码复用，不用写两个不同的 Loop 类。
    差异化的配置（策略）：MainLoop：被注入了“灵魂”（timercb_），所以它醒来后会执行清理任务。
IO Loop：没有注入回调，所以它保持“沉默”。
*/
void EventLoop::handleTimer() {
    readTimer(timerFd_);

    if(timercb_) timercb_();
    else {
        printf("EventLoop::handleTimer() tick.\n"); // 调试用心跳日志
    }
}

/*
runInLoop内两个分支的设置原因：
使得TcpServer::newconnection内的 Connection::connectEstablished 能通过 runInLoop 适配各种线程配置（无IO/从线程时由主线程负责IO,可走if (isInLoopThread()) {cb();}提高效率；有IO/从线程时，将对connect的操作全权交给IO从线程（走else分支））
且可以成为线程分流的兜底。
而Connection::send()的额外显式分流判断，是为了在最常见路径（Happy Path）上省去创建回调对象的开销。这是高性能库的典型特征——“能省一点是一点”。

***如果面试官问你：“为什么 Muduo 在 send 里要显式判断 loop 线程，而 runInLoop 里明明已经有了判断？”***

你可以直接抛出你的总结，并加上这句话：
“这体现了高性能网络库在‘接口通用性’和‘运行时性能’之间的取舍。runInLoop 负责保证逻辑的正确性和通用性（兜底），而在 send 这种高频调用的热点路径上，我们必须打破抽象，手动内联优化（Manual Inlining），以避免 std::function 和内存分配带来的昂贵开销。这是典型的‘二八定律’在代码优化中的体现。”
*/
void EventLoop::runInLoop(Functor cb){
    if(isInLoopThread()) cb();
    else queueInLoop(std::move(cb));
}
    
void EventLoop::queueInLoop(Functor cb) {
    {  // 加锁保护 pendingFunctors_
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(std::move(cb));
    }
    
    /*if (!isInLoopThread() || callingPendingFunctors_) wakeup();
    意图是：“我在执行当前这批任务时，如果有新任务加进来，请务必把闹钟（wakeupFd）再按响一次，防止我干完手里的活儿直接睡着，把新任务给忘了
    否则 if (!isInLoopThread()) wakeup(); 在这期间的新加任务就不会有通知
    */
    if(!isInLoopThread() || callingPendingFunctors_)
        wakeup();
}


// "Init Point/Container -> Lock -> Copy Pointer/Swap Container  -> Unlock -> Do Work"

void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    /* 为什么在执行任务队列时要用 `swap`？
    【核心设计：缩减临界区 (Critical Section) 与 保证回调安全】

    1. 性能优化（极简临界区）：
       从锁竞争角度看，我们将 std::vector 的 swap 操作（仅涉及三个指针拷贝，O(1)）放在锁内，
       而将实际耗时的回调执行移到了锁外。这极大地减小了锁的粒度，避免了工作线程在调用 runInLoop 
       投递任务时，因 IO 线程正在执行耗时回调而发生长时间的锁等待，实现了高并发下的“高吞吐”。

    2. 安全保障（避免死锁与崩溃）：
       如果直接持有锁遍历队列，一旦回调函数内又触发了 queueInLoop（尝试再次加锁），
       会导致死锁（若使用非递归锁）。
       此外，swap 让我们在操作局部变量 functors，避免了直接遍历 pendingFunctors_ 时，
       因回调函数新增任务导致 vector 扩容而引发的迭代器失效（Crash）问题。

    3. 调度公平性（防止 IO 饥饿）：
       从稳定性角度看，swap 相当于为当前轮次打了一个“快照”。
       如果直接遍历全局队列，且回调中不断递归投递新任务，IO 线程可能会陷入无限循环的任务处理中，
       导致无法及时回到 epoll_wait 响应高优先级的网络 IO 事件（即 IO 饥饿）。
       swap 保证了每一轮只处理“那一瞬间”积压的任务，新任务留待下一轮，实现了 IO 与计算的负载均衡。
    */
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for(const auto& func : functors)
        func();

    callingPendingFunctors_ = false;
}

    
    