#include <myreactor/EventLoopThreadPool.h>
#include <myreactor/EventLoopThread.h>
#include <myreactor/EventLoop.h>

using namespace myreactor;
/*
class EventLoopThreadPool{
private:
    EventLoop* mainloop_;
    bool started_;
    int numThreads_;
    int next_; // 轮询下标

    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> subloops_;

public:
    EventLoopThreadPool(EventLoop* mainloop, int numThreads_);
    ~EventLoopThreadPool();

    void start();

    // 核心方法：RR 轮询获取下一个 Loop
    EventLoop* getNextLoop();

    // 获取所有的 Loop (可能用于广播)
    std::vector<EventLoop*> getAllLoops();

    bool isstarted() const;
    
};
*/

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
        // 如果没有，作用域结束 IO 线程对象就会就会析构
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
