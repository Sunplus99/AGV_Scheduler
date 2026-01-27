#pragma once
#include <string>
/*
配合 Reactor（微秒级）和 AGV（毫秒级），这个类必须同时支持两种转换，并且提供比较运算符（方便 TcpServer 判断超时）
*/
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
    // // 给 EventLoop/Timer 用 (秒)
    // time_t secondsSinceEpoch() const { return static_cast<time_t>(msSinceEpoch_ / 1000000); }

    // 辅助工具 (给 TcpServer 判超时用)
    // 返回两个时间戳的时间差(秒)
    // static double timeDifference(Timestamp high, Timestamp low) {
    //     int64_t diff = high.msSinceEpoch() - low.msSinceEpoch();
    //     return static_cast<double>(diff) / 1000000.0;
    // }

    // 运算符重载 (方便比较) rhs : right hand side;运算符右侧的操作数
    // bool operator<(const Timestamp& rhs) const { return msSinceEpoch_ < rhs.msSinceEpoch_; }
    // bool operator==(const Timestamp& rhs) const { return msSinceEpoch_ == rhs.msSinceEpoch_; }
    
    // // 判断是否有效
    // bool valid() const { return msSinceEpoch_ > 0; }

    // // 增加一段时间 (秒)
    // Timestamp addTime(double seconds) const {
    //     int64_t delta = static_cast<int64_t>(seconds * 1000000);
    //     return Timestamp(msSinceEpoch_ + delta);
    // }
};

}