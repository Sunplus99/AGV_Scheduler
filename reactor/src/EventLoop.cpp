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

int EventLoop::createEventfd(){
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(evtfd < 0){
        perror("eventfd create error");
        exit(1);
    }
    return evtfd;
}

int EventLoop::createTimerfd(){
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

} 


EventLoop::~EventLoop() {
    ::close(wakeupFd_);
    if (timerFd_ != -1) ::close(timerFd_);
    delete ep_;
}

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

        for(auto ch : activeChannels_)
            ch->handleEvent();

        // 5. 处理跨线程任务
        doPendingFunctors();
    }

    
}


void EventLoop::stop(){
    if(quit_) return; // 幂等性检查：如果已经停了，直接返回
    
    quit_ = true;
    
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

        // 启动定时器
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


void EventLoop::handleTimer() {
    readTimer(timerFd_);

    if(timercb_) timercb_();
    else {
        printf("EventLoop::handleTimer() tick.\n"); // 调试用心跳日志
    }
}


void EventLoop::runInLoop(Functor cb){
    if(isInLoopThread()) cb();
    else queueInLoop(std::move(cb));
}
    
void EventLoop::queueInLoop(Functor cb) {
    {  // 加锁保护 pendingFunctors_
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(std::move(cb));
    }
    
    if(!isInLoopThread() || callingPendingFunctors_)
        wakeup();
}


// "Init Point/Container -> Lock -> Copy Pointer/Swap Container  -> Unlock -> Do Work"

void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for(const auto& func : functors)
        func();

    callingPendingFunctors_ = false;
}

    
    