#include <myreactor/ThreadPool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <string.h>

using namespace myreactor;
/*
class ThreadPool{
private:
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> taskqueue_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic_bool stop_;
public:
    ThreadPool(size_t threadnum);

    void addtask(stdL::function<void()> task);

    ~ThreadPool();

};

*/

/*
生产者用的std::lock_guard和消费者用的std::unique_lock，是对同一个mutex_（线程池的共享互斥锁）的封装，它们锁定的是同一把底层互斥锁 。两者的区别仅在于锁的封装类型（灵活性不同），而非锁定的互斥锁对象不同。
*/

/*
主线程的视角：串行循环创建线程，只负责 “生” 不负责 “管”,以主线程的执行流来看，代码就是一个普通的for循环
子线程的视角：被创建后立即 “独立运行”，直接进入 “工作待命” 状态。每个子线程被emplace_back创建的瞬间，就会脱离主线程的控制，由操作系统的线程调度器接管，立即执行绑定的 lambda 代码
子线程的行为是独立且并发的，主线程的创建流程和子线程的执行流程也是并发的
*/

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
            
            /*while(true){
                std::function<void()> task;

                {   // 临界资源(taskqueue_)开始 ///////////////////////////////
                    std::unique_lock<std::mutex> lock(this->mutex_);

                    // 等待生产者的条件变量
                    this->condition_.wait(lock, [this]{
                        return ( (this->stop_==true) || (this->taskqueue_.empty()==false) ); // 防止：1.虚假唤醒 2.惊群后资源被截胡
                    }); 

                    //在线程池停止之前，如果队列中还有任务，执行完再退出。
                    if( (this->stop_==true) && (this->taskqueue_.empty()==true) ) return; 

                    // 出队一个任务
                    task = std::move(this->taskqueue_.front()); // 移动语义优化
                    this->taskqueue_.pop();
                }   // 临界资源结束           /////////////////////////////////

                printf("%s thread %ld is execute task.\n", threadtype_.c_str(), syscall(SYS_gettid));
                task();
            }*/
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    
                    // 1. 等待 (和 Log 唯一的区别是 wait vs wait_for)
                    /*
                        while (!stop_ && taskqueue_.empty()) { // 注意这里是 while，不是 if
                            condition_.wait(lock);
                        }
                    */
                    condition_.wait(lock, [this]{ 
                        return stop_ || !taskqueue_.empty(); 
                    });

                    // 2. 退出检查 
                    // 即使被唤醒，也可能是因为 stop 中的通知，如果没任务了，再彻底退出
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
