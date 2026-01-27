#pragma once
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>

namespace myreactor{

class EventLoop;

class EventLoopThread{
private:
    EventLoop* loop_;
    std::thread thread_;
    std::mutex mutex_; // 利用锁实现从线程创建loop，主线程获得iOloop的 【同步控制】
    std::condition_variable cond_; //同步控制

    // IO线程主函数
    void threadFunc();

public:
    EventLoopThread();
    ~EventLoopThread();

    // 启动线程，并返回该线程中创建的 Eventloop 指针
    EventLoop* startLoop();
};

}