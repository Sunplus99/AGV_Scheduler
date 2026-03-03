#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

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