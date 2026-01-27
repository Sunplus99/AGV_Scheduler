#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <cstdio>
#include <memory>
#include <vector>

/*
在高性能场景下，确实存在两个性能瓶颈，这正是引入“异步日志”要解决的：
    直接磁盘 I/O：fprintf(log_file_...) 是直接写文件，磁盘速度比内存慢几千倍。
    频繁 Flush：fflush(log_file_) 被放在了每次 Log 调用中，这意味着每打印一行日志，就要强制操作系统把缓冲区刷到磁盘，这是极大的性能杀手。
改造架构示:
    前端 (Logger)：依然负责 vsnprintf 格式化字符串，然后将生成的 std::string 丢进一个队列，立马返回，不等待写盘。
    后端 (AsyncLogging)：一个新的类，拥有一个独立线程。当队列里有数据，或者超过 3 秒钟（超时），它就醒来把队列里的所有数据“一锅端”写进磁盘。
*/

class AsyncLogging{
public:
    AsyncLogging(const std::string& basename);
    ~AsyncLogging();

    // 二段式初始化接口
    // 负责打开文件和启动线程
    // 返回 true 表示成功，false 表示失败 (例如文件无法打开)
    // 相比构造函数，它可以告诉调用者"是否成功"
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
