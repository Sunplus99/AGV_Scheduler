#pragma once

#include <mutex>
// #include <cstdio>
#include <string>
#include <memory>

// 日志级别
/*
开发阶段设level_ = DEBUG（0）：所有级别（DEBUG/INFO/WARN/ERROR/FATAL）都打印；
测试 / 生产阶段设level_ = INFO（1）：DEBUG（0 < 1）被过滤，只打印 INFO 及以上；
紧急故障排查时设level_ = ERROR（3）：只打印 ERROR/FATAL，减少日志量，聚焦核心问题。
*/
enum LogLevel {
    DEBUG = 0,
    INFO,
    WARN,
    ERROR,
    FATAL
};

class AsyncLogging;

class Logger {
public:
    // 创建与获取单例实例
    static Logger& Instance();

    // 设置日志级别
    void SetLevel(LogLevel level);

    // 打开日志文件, 名字默认是 agv_server.log
    bool Open(const std::string& filename = "agv_server.log");

    // 核心打印函数
    /*
    C 风格可变参数有一个强制要求：... 必须是函数参数列表的最后一个参数，且前面必须有至少一个 “固定参数”（这里的 format 就是关键的固定参数
    */
    void Log(LogLevel level, const char* file, int line, const char* format, ...); 

private:
    /*
    构造 / 析构设为private，外部无法用new Logger()或Logger logger创建实例，只能通过Instance()获取唯一实例。
    */
    Logger();
    ~Logger() = default;

    /*
    禁止拷贝 : 防止用户误写Logger logger = Logger::Instance()，导致创建新实例，破坏单例特性。
    */
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    std::mutex mutex_;
    LogLevel level_;
    
    /*
    FILE* 是 C 标准库中用于文件操作的核心类型（头文件 <cstdio>/<stdio.h>），可以通俗理解为 “文件句柄”—— 它是程序和操作系统之间沟通 “已打开文件” 的 “桥梁”，也是日志类扩展 “文件输出” 的关键。 
        FILE 是 C 标准库定义的一个结构体（不用关心内部细节，先只需知道它封装了文件的所有上下文：比如文件路径、读写位置、缓冲区、打开模式等）；
        FILE* 是指向这个结构体的指针，*一个有效的 FILE 就代表一个 “已打开的文件”**（无效则为 nullptr，比如文件不存在且无法创建）。
    日志类是偏 “轻量级、高性能” 的设计，FILE* 相比 C++ 的 std::ofstream 有两个核心优势：
        性能更高：C 风格的 fprintf 比 C++ 的 cout/fstream 更少的封装开销，日志是高频调用场景，这点很重要；
        线程安全适配性更好：结合日志类的 mutex 锁，fprintf 是原子性更强的写入操作，不容易出现日志乱序；
        跨平台兼容性：C 标准库的 FILE* 在所有平台（Linux/Windows/macOS）行为一致，无需适配。
    核心操作：fopen（打开，用 "a" 模式）→ fprintf（写入）→ fclose（关闭）；
    */
    // FILE* log_file_;

    std::unique_ptr<AsyncLogging> async_logger_;
};

// ===========================================
// 定义宏
// ===========================================

/*
__VA_ARGS__ + ##
    __VA_ARGS__：是编译器预定义的宏，作用是把宏调用时传入的可变参数，原封不动替换到这个位置。
    ## 的核心作用是：如果 __VA_ARGS__ 为空（即宏调用时没有传可变参数），就自动删除它前面的逗号。
*/
/*为什么 LOG_INFO 必须是宏？
LOG_INFO 之所以被定义为宏，不是为了“省代码”，而是为了“抓取上下文”。
只有在预处理阶段（Macro Expansion），__FILE__ 和 __LINE__ 才会展开为当前调用这行日志的代码所在的文件名和行号
如果写成普通函数 void LogInfo(...)，那么 __LINE__ 永远是 LogInfo 函数定义的那一行，而不是你调用日志的那一行。这就失去了查错的意义
*/
#define LOG_DEBUG(format, ...) \
    Logger::Instance().Log(DEBUG, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_INFO(format, ...) \
    Logger::Instance().Log(INFO, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_WARN(format, ...) \
    Logger::Instance().Log(WARN, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_ERROR(format, ...) \
    Logger::Instance().Log(ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_FATAL(format, ...) \
    Logger::Instance().Log(FATAL, __FILE__, __LINE__, format, ##__VA_ARGS__)
    