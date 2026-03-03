#pragma once
#include <cstdint>
#include <string>

/* 消息类型枚举（协议的 “字典”）
定义所有通信消息的「类型标识」，让收发双方知道 “包体里装的是什么业务数据”（比如标识 1 = 登录请求，标识 3 = 心跳包）。
*/

namespace agv{
namespace protocol{

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