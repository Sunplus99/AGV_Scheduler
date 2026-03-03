#pragma once

#include <mutex>
// #include <cstdio>
#include <string>
#include <memory>

// 日志级别
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
    void Log(LogLevel level, const char* file, int line, const char* format, ...); 

private:
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
    
    // FILE* log_file_;

    std::unique_ptr<AsyncLogging> async_logger_;
};

// ===========================================
// 定义宏
// ===========================================


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
    