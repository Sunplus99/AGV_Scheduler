#include <myreactor/ThreadPool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <string.h>

using namespace myreactor;

ThreadPool::ThreadPool(size_t threadnum,const std::string& ttype)
    : stop_(false), 
      started_(false),
      threadnum_(threadnum),
      threadtype_(ttype) 
{  }

void ThreadPool::start() {
    if(started_ == true) return ;
    started_ = true;
    stop_ = false; // 重启场景，确保 stop复位

    for(int ii=0 ; ii<threadnum_ ; ++ii){
        threads_.emplace_back([this] {
            printf("create %s thread(%ld).\n", threadtype_.c_str(), syscall(SYS_gettid));
            
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    
                    // 1. 等待 (和 Log 唯一的区别是 wait vs wait_for)
                    condition_.wait(lock, [this]{ 
                        return stop_ || !taskqueue_.empty(); 
                    });

                    // 2. 退出检查 
                    if (stop_ && taskqueue_.empty()) {
                        break;
                    }

                    // 3. 取数据 (和 Log 的区别：One-by-One vs Batch)
                    task = std::move(taskqueue_.front());
                    taskqueue_.pop();
                }

                // 4. 执行
                if(task) task();
            }
        });
    }

}

void ThreadPool::addtask(std::function<void()> task){
    {   // 临界资源(taskqueue_)开始 /////////////////////////////////
        std::lock_guard<std::mutex> lock(mutex_);
        taskqueue_.push(std::move(task));
    }   // 临界资源(taskqueue_)开始 /////////////////////////////////

    condition_.notify_one();
}

ThreadPool::~ThreadPool(){
    stop(); // 兜底，防止用户忘了调 EchoServer::Stop
}

void ThreadPool::stop(){
    if(stop_) return; // 幂等性检查：如果已经停了，直接返回 
    stop_ = true;
    // 线程可能正阻塞在 wait_for 上，必须叫醒它，让它通过逻辑检测并退出
    condition_.notify_all();
    for(std::thread& th : threads_){
        if(th.joinable())  th.join();
    }

    started_ = false;
    threads_.clear();  // 方便下次 start

}

size_t ThreadPool::load_size(){
    std::lock_guard<std::mutex> lock(mutex_);
    return taskqueue_.size();
}

size_t ThreadPool::size(){
    return threads_.size();
}
