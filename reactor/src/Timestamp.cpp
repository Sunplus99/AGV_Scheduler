#include <myreactor/Timestamp.h>
#include <sys/time.h>
// #include <time.h>
#include <stdio.h>
#include <chrono>

using namespace myreactor;


Timestamp::Timestamp() : usSinceEpoch_(0) {}

Timestamp::Timestamp(int64_t usSinceEpoch) 
    : usSinceEpoch_(usSinceEpoch) {}


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

