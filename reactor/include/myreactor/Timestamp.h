#pragma once
#include <string>

namespace myreactor{

class Timestamp{
private:
    int64_t usSinceEpoch_;  // 微秒

public:
    Timestamp();
    explicit Timestamp(int64_t usSinceEpoch);

    static Timestamp now(); // 当前时间

    // 给 Logger 用 ：格式化输出 
    std::string toFormattedString(bool showMs = true) const;

    //  给 Reactor 层用 ：微秒 ; 接口不变了
    int64_t usSinceEpoch() const {return usSinceEpoch_;}
    // 给 AGV 协议用 ：毫秒
    int64_t toMilliseconds() const { return usSinceEpoch_ / 1000; }


    // 运算符重载：时间差（返回微秒）
    int64_t operator-(const Timestamp& rhs) const {
        return usSinceEpoch_ - rhs.usSinceEpoch_;
    }

};

}