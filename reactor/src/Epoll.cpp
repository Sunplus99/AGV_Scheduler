#include <myreactor/Epoll.h>
#include <myreactor/Channel.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

using namespace myreactor;
/*
class Epoll{
private:
    const int epollfd_;
    static const int MaxEvents = 100;
    struct epoll_event events_[MaxEvents];
public:
    Epoll();
    ~Epoll();

    // 核心方法 1.epoll_ctl()系统调用，供Eventloop使用 2.epoll_wait()，既返回事件数量，又更新events_成员，供Eventloop使用
    void update(int op, Channel *ch);  // 更新选项op由Eventloop判断并指示

    int wait(int timeout=-1);

    struct epoll_event* getReadyEvents();
};
*/

// 错误！！！
/*const int epollfd_; 作为类的const成员变量，必须在构造函数的初始化列表中完成初始化，无法在构造函数体中进行初始化；
构造函数体内部对epollfd_的=操作不是初始化(变量创建时（内存分配后立即赋值）)，而是赋值(变量创建后（已存在内存中）)，而const成员变量一旦初始化后就变为只读，不允许被赋值，因此会触发编译错误。
Epoll::Epoll() {
    if((epollfd_ = ::epoll_create(1)) < 0){
        printf("%s:%s:%d epoll create error:%d\n",__FILE__,__FUNCTION__,__LINE__,errno);
        exit(-1);
    }
}*/

/*
C++ 中，当创建一个类的对象时，构造函数的执行分为两个阶段：
成员初始化阶段：按成员变量在类中声明的顺序，依次初始化所有成员变量（包括const、引用、普通成员）。这个阶段对应构造函数的初始化列表，是const和引用成员唯一的初始化机会。
构造函数体执行阶段：执行构造函数{}内的代码，此时所有成员变量已经完成初始化，内存已分配且不可变（const成员）。
*/

// int Epoll::createEpollfd(){
//     int fd = ::epoll_create(1);
//     if(fd < 0){
//         printf("%s:%s:%d epoll create error:%d\n",__FILE__,__FUNCTION__,__LINE__,errno);
//         exit(-1);
//     }
//     return fd;
// }

/*
exec() 的核心是用新程序覆盖当前进程，但它有一个特性：默认保留父进程打开的文件描述符（FD）。除非该 FD 设置了 FD_CLOEXEC 标志，否则子进程会“无意识”地继承并持有这些 FD。
在 Reactor 模型中，如果 epollfd 没设置 CLOEXEC 就被子进程继承，会引发两个层面的严重泄露：
    首先是系统级文件描述符数量耗尽： 虽然内核里可能只有一个 eventpoll 对象，但系统级别的打开文件总数（System-wide open files）是按引用次数统计的。子进程每继承一次，系统总句柄数就 +1。频繁 fork()+exec() 会导致达到 fs.file-max 限制，导致全系统无法打开新文件。
    更严重的是逻辑资源死锁与僵尸连接：指的是Linux 内核通过引用计数管理对象生命周期。子进程继承 epollfd 后，内核中对应的 eventpoll 结构体引用计数增加。当父进程想关闭 Reactor、释放连接时，虽然父进程调用了 close(epollfd)，但因为子进程还拿着引用，计数不归零，内核就不会真正销毁该 epoll 对象。进而，该 epoll 对象内部引用的那些 TCP Socket 也无法释放，导致端口一直被占用（ESTABLISHED），成为“僵尸连接”。
而epoll_create1(EPOLL_CLOEXEC)就是通过给epollfd设置 CLOEXEC 标志，让exec()执行时自动关闭该 fd，避免此类泄露问题。
*/

Epoll::Epoll() : epollfd_(::epoll_create1(EPOLL_CLOEXEC)), events_(InitEventListSize){
    // 构造函数体：此时epollfd_已完成初始化，且是只读的
}

Epoll::~Epoll() {
    if(epollfd_ >= 0)
        ::close(epollfd_);
}

void Epoll::update(int op, Channel* ch){
    struct epoll_event ev;
    ev.events = ch->events();
    ev.data.ptr = ch;

    if(::epoll_ctl(epollfd_,op,ch->fd(),&ev) == -1){
        if(op == EPOLL_CTL_DEL){
            // 删除出错通常不致命（可能是重复删除），记录日志即可
            perror("epoll_ctl delete error (ignored)");
            return;
        }else{
            perror("epoll_ctl failed");
            exit(-1);
        }
    }
}

/*
1.::epoll_wait 每次能取出多少事件，与水平触发（LT）/ 边缘触发（ET）无关；
2.LT/ET 仅影响「内核是否生成事件通知」（即：内核是否把 fd 标记为 “就绪”，放入 epoll 的就绪事件队列）；
3.只要内核的就绪事件队列中存在「已生成的事件通知」（未被epoll_wait取完），下一轮EventLoop循环中的ep_->poll()（即epoll_wait）一定会继续取出这些剩余的已就绪事件，和触发模式无关。

水平触发（LT）：就绪状态持续通知 → 处理逻辑简化，无需循环，容错性高
    只要 fd 处于就绪状态，内核就持续生成事件通知，哪怕本次epoll_wait取出事件后没处理完（比如读缓冲区只读取了部分数据），下一轮epoll_wait依然会取出该 fd 的事件通知，继续处理。
    因此，LT 模式下无需额外用while()循环处理，直接单次处理即可，即使没处理完，后续也能收到通知补处理，开发难度低，容错性强（这也是epoll默认采用 LT 模式的原因）。
边缘触发（ET）：就绪状态变化仅通知一次 → 必须用while()循环 “干到位”，一次性处理完
    只有 fd 就绪状态发生变化时（比如从 “无数据”→“有数据”、从 “缓冲区满”→“缓冲区有空余”），内核才生成一次事件通知，后续即使 fd 仍处于就绪状态（比如读缓冲区还有剩余数据），也不会再生成通知。
    因此，ET 模式下必须用while()循环封装处理逻辑，一次性把当前就绪状态的资源处理完毕（比如读数据直到返回EAGAIN/EWOULDBLOCK，表示内核缓冲区已无数据），否则剩余数据会被遗漏，直到下一次 fd 就绪状态变化（比如有新数据写入）
*/

void Epoll::poll(int timeoutMs, std::vector<Channel*>* activeChannels) {
    int numEvents = ::epoll_wait(epollfd_, events_.data(), static_cast<int>(events_.size()), timeoutMs);
    // 紧贴着系统调用，没有任何其他代码插入,保存 errno，防止被后续代码修改
    int savedErrno = errno;

    if(numEvents > 0){
        fillActiveChannels(numEvents, activeChannels);
        // printf("Epoll::poll return %d events.\n", numEvents); // 加这一行
        // 扩容策略：如果这次事件填满了 vector，说明负载很高，下次多给点空间
        if(numEvents == static_cast<int>(events_.size())){
            events_.resize(events_.size()*2);
        }
    } else if(numEvents==0) { 
        return; //超时，什么都不做
    } else{
        if (savedErrno!=EINTR) {
            errno = savedErrno;
            perror("epoll_wait error");
            // 这里是否 exit 取决于策略，通常记录日志即可
        }
    }
}

// 好的设计应该由 Epoll 负责填充一个 std::vector<Channel*>，EventLoop 只拿结果，不关心底层是用 epoll 还是 poll
void Epoll::fillActiveChannels(int numEvents, std::vector<Channel*>* activeChannels) const {
    for(int i = 0; i<numEvents ; ++i){ //只会读取前 numEvents 个数据，后面的脏数据（Dirty Data）我们根本不关心, 【覆盖即清除】
        Channel* ch = static_cast<Channel*>(events_[i].data.ptr);
        ch->setrevents(events_[i].events);
        activeChannels->push_back(ch);
    }
}
