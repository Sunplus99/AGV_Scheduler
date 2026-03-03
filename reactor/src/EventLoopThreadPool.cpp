#include <myreactor/EventLoopThreadPool.h>
#include <myreactor/EventLoopThread.h>
#include <myreactor/EventLoop.h>

using namespace myreactor;


EventLoopThreadPool::EventLoopThreadPool(EventLoop* mainloop, int numThreads)
    : mainloop_(mainloop),
    //   started_(false),
      numThreads_(numThreads),
      next_(0)
{
}

EventLoopThreadPool::~EventLoopThreadPool() = default;

void EventLoopThreadPool::start() {
    // started_ = true;

    for(int i=0; i<numThreads_ ; ++i){
        // 创建 EventLoopThread 对象
        auto t = std::make_unique<EventLoopThread>();

        // 启动线程，并获取它内部创建的 Loop 指针
        subloops_.push_back(t->startLoop());

        // threads_ 接收所有权，负责“保命”（生命周期管理）
        threads_.emplace_back(std::move(t)); 
    }
}

// 轮询算法
EventLoop* EventLoopThreadPool::getNextLoop() {
    EventLoop* loop = mainloop_;

    if(!subloops_.empty()){
        loop = subloops_[next_];
        next_ = (next_+1) % subloops_.size();
    }

    return loop;
}
