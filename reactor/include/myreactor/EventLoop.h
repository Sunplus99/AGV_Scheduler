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
    

    // 2.唤醒相关
    int wakeupFd_;  // eventfd
    std::unique_ptr<Channel> wakeupChannel_;

    // 3.定时器机制
    int timerFd_;
    std::unique_ptr<Channel> timerChannel_;
    
    // 内部辅助函数
    static int createEventfd();  
    static int createTimerfd();
    static void readTimer(int timerfd);

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