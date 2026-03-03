#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <cstdio>
#include <memory>
#include <vector>


class AsyncLogging{
public:
    AsyncLogging(const std::string& basename);
    ~AsyncLogging();

    // 二段式初始化接口
    bool start();
    void stop();

    // 提供给Logger的核心接口
    void Append(std::string log_line);

private:
    // 线程运行的函数（死循环,但配以stop_以优雅结束）
    void ThreadFunc();


private:
    // 文件控制
    std::string basename_;
    FILE* fp_;

    // 线程控制
    bool stop_;  // 线程运行标志位 (控制线程退出)
    std::unique_ptr<std::thread> thread_;

    // 并发保护 ; 同步控制
    std::mutex mutex_;
    std::condition_variable cond_;

    // 数据缓冲 : 批量日志行
    std::vector<std::string> current_buffer_;
};
