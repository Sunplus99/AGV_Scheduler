#pragma once
#include <cstdint>
#include <string>

/* 消息类型枚举（协议的 “字典”）
定义所有通信消息的「类型标识」，让收发双方知道 “包体里装的是什么业务数据”（比如标识 1 = 登录请求，标识 3 = 心跳包）。
*/

namespace agv{
namespace protocol{

/*
普通枚举（enum MsgType { LOGIN_REQ = 1 };）有两个核心问题：
    作用域污染：枚举值（比如 LOGIN_REQ）会暴露在全局作用域，和其他枚举 / 变量重名时冲突；
    隐式类型转换：可以直接把枚举值当 int 用（比如 int x = LOGIN_REQ;），容易出错。
enum class（C++11 引入的强类型枚举）解决了这些问题：
    作用域隔离：枚举值必须通过 MsgType::LOGIN_REQ 访问，不会污染全局；
    无隐式转换：不能直接写 int x = MsgType::LOGIN_REQ;，必须显式转换 int x = static_cast<int32_t>(MsgType::LOGIN_REQ);，类型更安全；
    可指定底层类型：这就是 : int32_t 的作用,跨平台一致性.
    
enum class MsgType : int32_t 里的 : int32_t 仅指定枚举值的底层存储为 4 字节 int32_t，而 enum class 的强类型特性会让枚举类型和 int32_t 严格隔离，无法隐式转换；因此 head.type（int32_t）和 MsgType::LOGIN_RESP（MsgType 类型）必须显式强转才能比较，这是 enum class 的语法规则，和指定底层类型无关。
*/
enum class MsgType : int32_t {
    UNKNOWN        = 0,

    // 1. 连接与认知
    LOGIN_REQ      = 1, // 登录请求
    LOGIN_RESP     = 2, // 登录响应

    // 2. 存活检测
    HEARTBEAT      = 3, // 心跳包 (Keep-Alive)

    // 3.核心业务
    TASK_REQUEST   = 4, // 任务下发 (Server -> Client)
    TASK_REPORT    = 5, // 任务状态上报 (Client -> Server)

    // 4.寻路业务
    PATH_REQ     = 10, // AGV 请求寻路
    PATH_RESP    = 11  // Server 返回路径
};


inline std::string msgTypeToString(MsgType type) {
    switch (type) {
        case MsgType::LOGIN_REQ:    return "LOGIN_REQ";
        case MsgType::LOGIN_RESP:   return "LOGIN_RESP";
        case MsgType::HEARTBEAT:    return "HEARTBEAT";
        case MsgType::TASK_REQUEST: return "TASK_REQUEST";
        case MsgType::TASK_REPORT:  return "TASK_REPORT";
        case MsgType::PATH_REQ:     return "PATH_REQ";
        case MsgType::PATH_RESP:    return "PATH_RESP";
        default: return "UNKNOWN(" + std::to_string((int32_t)type) + ")";
    }
}


}
}