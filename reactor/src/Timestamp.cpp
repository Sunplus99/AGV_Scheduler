#include <myreactor/Timestamp.h>
#include <sys/time.h>
// #include <time.h>
#include <stdio.h>
#include <chrono>

using namespace myreactor;

/*
    Timestamp();
    explicit Timestamp(int64_t msSinceEpoch);

    static Timestamp now(); // 当前时间
    std::string toString() const;

    int64_t msSinceEpoch() const;
*/

Timestamp::Timestamp() : usSinceEpoch_(0) {}

Timestamp::Timestamp(int64_t usSinceEpoch) 
    : usSinceEpoch_(usSinceEpoch) {}


/*
std::chrono（C++11+）实现 : 跨平台（Linux/macOS/Windows/ 嵌入式）
gettimeofday 实现 : 仅支持 POSIX 系统（Linux/Unix/macOS），Windows 无此函数
*/
Timestamp Timestamp::now() {
    // 现代 C++ 写法，微秒级精度  microseconds 是微秒，缩写 us
    auto now_time = std::chrono::system_clock::now();
    auto duration_in_us = std::chrono::duration_cast<std::chrono::microseconds>(now_time.time_since_epoch());
    return Timestamp(duration_in_us.count());
}

std::string Timestamp::toFormattedString(bool showMs) const {
    char buf[64] = {0};
    time_t seconds = static_cast<time_t>(usSinceEpoch_ / 1000000);
    struct tm tm_time;
    localtime_r(&seconds, &tm_time);

    if (showMs) {
        int microseconds = static_cast<int>(usSinceEpoch_ % 1000000);
        snprintf(buf, sizeof(buf), "%4d-%02d-%02d %02d:%02d:%02d.%06d",
                 tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                 tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec, microseconds);
    } else {
        snprintf(buf, sizeof(buf), "%4d-%02d-%02d %02d:%02d:%02d",
                 tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                 tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
    }
    return std::string(buf);
}
/*
struct timeval
{
    long tv_sec;  秒
    long tv_usec; 微秒
};
*/

// Timestamp Timestamp::now(){
//     struct timeval tv;
//     gettimeofday(&tv, NULL);
//     // 返回当前距离1970年的秒数和微妙数，后面的tz是时区，一般不用
//     return Timestamp(tv.tv_sec*1000*1000 + tv.tv_usec);
// }

// std::string Timestamp::toString() const {
//     char buf[128] = {0};
//     int64_t seconds = msSinceEpoch_ / 1000000;
//     int64_t microseconds = msSinceEpoch_ % 1000000;
    
//     struct tm tm_time;
//     // 使用线程安全的 localtime_r
//     localtime_r((time_t*)&seconds, &tm_time);

//     snprintf(buf, sizeof(buf), "%4d-%02d-%02d %02d:%02d:%02d.%06ld", 
//             tm_time.tm_year + 1900,
//             tm_time.tm_mon + 1,
//             tm_time.tm_mday,
//             tm_time.tm_hour,
//             tm_time.tm_min,
//             tm_time.tm_sec,
//             microseconds);
//     return std::string(buf);
//     /*
//     tm结构体只能精确到秒。所以把seconds = microSecondsSinceEpoch_ / 1000000作为信息源传入localtime_r，并把声明的tm结构体tm_time作为接受容器，最后补足microseconds
//     既兼顾了tm结构体的便捷性，又保留了微秒级高精度，非常适合 Reactor 项目中日志记录、事件计时等需要高精度时间的场景
//     */

//     /*
//     安全格式化：用snprintf而非sprintf，snprintf的第二个参数sizeof(buf)会限制写入字节数（最多写入127个字节，最后自动添加'\0'结束符），避免缓冲区溢出，杜绝因格式错误导致的程序崩溃。
//     */
// }
/*
std::move的本质：仅将一个「左值」转换为「右值引用」，本身不做任何 “移动资源” 的操作，它只是给编译器一个 “可以触发移动构造 / 移动赋值” 的信号；
std::move的适用前提：被转换的对象必须是支持移动语义的 C++ 类对象（比如std::string、std::vector等），这些对象拥有「可转移的堆上资源」（比如std::string的堆上字符缓冲区），移动操作就是转移这份堆资源的所有权，避免深拷贝；
*/

