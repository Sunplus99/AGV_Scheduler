#pragma once
#include <vector>
#include <memory>
#include <functional>

namespace myreactor{  // “命名空间重组（Namespace Reopening）”，或者通俗点叫 “打开追加”。

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool{
private:
    EventLoop* mainloop_;
    // bool started_;
    int numThreads_;
    int next_; // 轮询下标

    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> subloops_;

public:
    EventLoopThreadPool(EventLoop* mainloop, int numThreads);
    ~EventLoopThreadPool();

    void start();

    // 核心方法：RR 轮询获取下一个 Loop
    EventLoop* getNextLoop();

    // bool isstarted() const {return started_;}

};


}