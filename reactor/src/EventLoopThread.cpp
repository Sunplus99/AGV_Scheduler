#include <myreactor/EventLoopThread.h>
#include <myreactor/EventLoop.h>
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */

using namespace myreactor;

EventLoopThread::EventLoopThread(): loop_(nullptr) {}

EventLoopThread::~EventLoopThread() {
    if(loop_) loop_->stop();
    if(thread_.joinable()) thread_.join();
}

EventLoop* EventLoopThread::startLoop(){
   // 分离出IO从线程，从线程立马执行 threadFunc
   /*
   IO 线程池 这种“一旦创建，终身绑定，死循环陷入”的设计，正是 Reactor 模式 (One Loop Per Thread) 的精髓所在。
   */
   thread_ = std::thread(&EventLoopThread::threadFunc, this);
   
   EventLoop* loop = nullptr;
   {
        // 主线程在这里阻塞等待，直到子线程完成 Loop 的初始化 
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] {return loop_ != nullptr;});
        loop = loop_;
   }
   return loop;
}

void EventLoopThread::threadFunc(){
    // 1. 在栈上创建一个 EventLoop
    //    此时 EventLoop 构造函数执行，记录的是当前子线程 ID！
    printf("create IOthread(%ld).\n", syscall(SYS_gettid));
    EventLoop loop;

    {
        // 2. 将 loop 指针暴露给成员变量，并唤醒主线程
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }

    // 3. 开始事件循环（死循环，直到 stop 被调用）
    loop.run();

    // 4. 循环结束，清理指针
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = nullptr;
}