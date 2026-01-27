#include <utils/Logger.h>
// #include <ctime>
#include <cstdarg>
#include <utils/AsyncLogging.h>
#include <myreactor/Timestamp.h>
// ==========================================
// ANSI 颜色代码 (终端专用)
// ==========================================
// 美国国家标准协会（American National Standards Institute）
// 转义字符 : \033 或 \x1b
// \033[<属性>;<前景色>;<背景色>m<要显示的文字>\033[0m
#define ANSI_COLOR_RED      "\x1b[31m"
#define ANSI_COLOR_GREEN    "\x1b[32m"
#define ANSI_COLOR_YELLOW   "\x1b[33m"
#define ANSI_COLOR_BLUE     "\x1b[34m"
#define ANSI_COLOR_MAGENTA  "\x1b[35m"
#define ANSI_COLOR_CYAN     "\x1b[36m"
#define ANSI_COLOR_RESET    "\x1b[0m"

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

/*在构造函数中打开资源有【隐患】：
    构造函数没有返回值。
        如果 fopen 失败了（比如磁盘满了、没有权限、路径不存在），会发生什么：
        ①无法返回错误码：你不能 return false 告诉调用者“Logger 创建失败了”。
        ②对象处于“半死不活”状态 (Zombie Object)：构造函数执行完了，Logger 对象创建出来了，但 log_file_ 是 nullptr。后续如果你调用 Log() 方法，如果没有严格的判空检查，程序直接 SegFault (段错误) 崩溃。
        ③异常处理 (Exception)：在 C++ 标准中，构造函数失败应该抛出异常 (throw std::runtime_error)。但在很多高性能服务器或 Google C++ 规范中，禁用异常（为了性能和代码可控性）。
    为了解决“构造函数无法报错”的问题，工业界（如游戏引擎、高性能服务器）通常采用 “二段式初始化” (Two-phase Initialization)，即：
        第一阶段 (构造)：只做最简单的成员变量赋初值（如指针置空），绝对不执行可能失败的 IO 操作。
        第二阶段 (Init)：提供一个 Init() 或 Open() 函数，执行 IO 操作，并返回 bool 或错误码。
*/
// 构造函数：默认打开 agv_server.log
Logger::Logger() : level_(INFO), async_logger_(nullptr) {}

// Logger::~Logger() {}

/*bool Logger::Open (const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 支持“切换”文件：如果之前有打开的，先关掉,防止句柄泄漏
    if (log_file_) {
        fclose(log_file_);
    }

    
    a : Open  for  appending  (writing  at end of file).  The file is created if it does not exist.  The stream is positioned at the end of the file.
    
    log_file_ = fopen(filename.c_str(), "a");
    if (!log_file_) {
        fprintf(stderr, "Logger Open Failed: %s\n", filename.c_str());
        return false;
    }

    return true;
}*/

bool Logger::Open (const std::string& filename) {
    // 1. 先尝试在旁边偷偷创建一个新的（还没动旧的,也没打开新文件）
    auto new_logger = std::make_unique<AsyncLogging>(filename);

    // 2.初始化
    /*如果底层文件打开失败，Logger 应该也返回失败
      此时 new_logger 会被自动析构，不会有内存泄漏*/
    if(!new_logger->start()) 
        return false;

    // 3.只有初始化成功了，才替换掉旧的 logger
    std::lock_guard<std::mutex> lock(mutex_);
    async_logger_ = std::move(new_logger);

    return true;
}

void Logger::SetLevel(LogLevel level) {
    level_ = level;
}

void Logger::Log(LogLevel level, const char* file, int line, const char* format, ...) {
    // 级别过滤（性能优化）
    if (level < level_) return;

    // 1.准备时间字符串
    std::string time_str = myreactor::Timestamp::now().toFormattedString(false); // false 表示值要秒级
    // time_t now = time(nullptr);
    // tm* tm_now = localtime(&now);
    // char time_str[32];
    // /*
    // %Y 带世纪部分的十制年份 ; %m 十进制表示的月份 ;%d 十进制表示的每月的第几天 ; 
    // %H 24小时制的小时; %M 十时制表示的分钟数 ; %S 十进制的秒数
    // */
    // strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_now);

    // 2.准备级别字符串 : 名称字符与颜色 选定
    const char* level_str = "";  // 常量指针，可以改变指向，但不能改变指向的内容
    const char* color_code = "";

    switch(level){
        case DEBUG:
            level_str = "[DEBUG]";
            color_code = ANSI_COLOR_CYAN; 
            break;
        case INFO:
            level_str = "[INFO ]";
            color_code = ANSI_COLOR_GREEN;
            break;
        case WARN:
            level_str = "[WARN ]";
            color_code = ANSI_COLOR_YELLOW;
            break;
        case ERROR:
            level_str = "[ERROR]";
            color_code = ANSI_COLOR_RED;
            break;
        case FATAL:
            level_str = "[FATAL]";
            color_code = ANSI_COLOR_MAGENTA;
            break;
    }

    // 3.准备 格式化的日志内容 字符串
    /*
    只要调用了 va_start，就必须配对调用 va_end，这是 C 标准的硬性规定
    */
    char msg_buf[4096];
    va_list args; // va_list：C 标准库的可变参数类型，用于遍历...中的参数；
    va_start(args, format); // va_start(args, format)：第一个参数是va_list，第二个是可变参数前的最后一个固定参数（此处为format）；
    vsnprintf(msg_buf, sizeof(msg_buf), format, args);
    va_end(args);

    /*
    vsnprintf：最后一个参数必须是 va_list 类型（也就是宏 va_start 处理后的那个东西）。它是专门用来处理可变参数列表的。
    snprintf：接受确定的、具体的参数（或者 ... 可变参数），就像 printf 一样。
    */

    // 4.组装 完全字符串
    char total_buf[4096 + 128];
    int len = snprintf(total_buf, sizeof(total_buf), "%s %s [%s:%d] %s\n", time_str.c_str(),level_str, file, line, msg_buf);

    // 5.推送到异步队列
    if(async_logger_) {
        // 跨线程，延长栈数据的生命 : 这里的 string 构造会发生一次内存拷贝，是必须的
        async_logger_->Append(std::string(total_buf, len));
    }

    // 6.控制台输出
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fprintf(stdout, "%s %s%s%s [%s:%d] %s\n",time_str.c_str(), color_code,level_str,ANSI_COLOR_RESET, file, line, msg_buf);
    }
    /*
    // 4.加锁并打印
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 输出到控制台(带颜色)
        
        printf 是 fprintf 的“特化版”——固定输出到标准输出（stdout）
            int printf(const char *format, ...) {
                return fprintf(stdout, format, ##__VA_ARGS__);
            }
        第一个参数 FILE* stream 允许指定输出目标—— 可以输出到控制台、日志文件、甚至串口 / 网络（只要封装成 FILE*），而 printf 只能 “死磕” 标准输出（stdout）。
        
        // 关键补充：stdout 可以被 “重定向”（工业级场景必知）
        fprintf(stdout, "%s %s%s%s [%s:%d] %s\n",time_str, color_code,level_str,ANSI_COLOR_RESET, file, line, msg_buf);

        // 输出到文件
        if(log_file_) {
            fprintf(log_file_, "%s %s [%s:%d] %s\n",time_str, level_str, file, line, msg_buf);
        }
        
        // 立即刷新缓冲区，防止程序崩溃时日志丢失
        
        fflush() 是 C 标准库中的一个函数，它将缓冲区中的数据立即写入到与流关联的文件或设备中。
        
        fflush(log_file_);

    }*/

}