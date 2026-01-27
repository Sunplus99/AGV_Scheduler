#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
/* 异步日志
1. 为什么不建议直接使用 myreactor/ThreadPool？
你可能会想：“既然我已经有了线程池，把写日志的任务 throw 进去不就行了吗？” 虽然逻辑上可行，但在高性能场景下，这样做有三个致命缺陷：
    资源争抢 (Resource Contention)：
        ThreadPool 是为了处理业务逻辑（计算、数据库查询、RPC调用）设计的。如果日志量非常大（例如调试期间），大量的日志任务会瞬间塞满线程池的任务队列，导致正常的业务请求处理被延迟，甚至引发“雪崩”。
    顺序性难以保证 (Ordering)：
        线程池里的线程是并发执行的。如果你先 log 了一句 "A started"，紧接着 log 了一句 "A finished"。在通用线程池中，这两个任务可能被不同的线程抢到，"A finished" 甚至可能比 "A started" 先写入文件，这会让查日志变得极其痛苦。
    磁盘 I/O 阻塞：
        日志写入最终是磁盘 I/O。如果线程池的工作线程因为写日志被阻塞（Disk Busy），那么这个线程就无法处理业务请求了。
*/
namespace myreactor{

class ThreadPool{
private:
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> taskqueue_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic_bool stop_;
    bool started_ = false; // 启动优化标志
    int threadnum_;
    const std::string threadtype_;
public:
    ThreadPool(size_t threadnum, const std::string& ttype = "WORKER");
    ~ThreadPool();

    void addtask(std::function<void()> task);
    //  load_size() 方法查看负载
    size_t load_size();
    size_t size();
    
    void start();
    void stop();
};

}