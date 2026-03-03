#pragma once

#include "utils/json.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

using json = nlohmann::json;
using AgvId = int32_t;

/*
在 AGV/AMR（自主移动机器人）领域，最权威的通信接口标准是 VDA 5050（由德国汽车工业协会制定）。它的目的是让不同厂商的 AGV 能在同一个调度系统下工作。
*/


namespace agv{
namespace model{

// ==========================================
// 1. 基础模型 (Basic Models)
// ==========================================
/*
Point 是个聚合体，应该使用 大括号 {} 来进行聚合初始化
*/
struct Point {
    int x = 0;
    int y = 0;

    // 重载 == 用于比较
    bool operator==(const Point& p) const {
        return x==p.x && y==p.y;
    }

    // 重载 < 用于 map/set 的 Key
    bool operator<(const Point& p) const {
        if(x != p.x) return x < p.x;
        return y < p.y;
    }

    // 方便日志打印
    std::string toString() const {
        return "C" + std::to_string(x) + "," + std::to_string(y) + ")";
    }

};

// Point 序列化与返序列化
inline void to_json(json& j, const Point& p) {
    j = json{{"x", p.x},{"y", p.y}};
    // 等价写法：
    // j["x"] = p.x; j["y"] = p.y;
}


inline void from_json(const json& j, Point& p) {
    // j.at("x")：严格取值，键不存在会抛异常（防御性编程，避免非法数据）
    j.at("x").get_to(p.x);
    j.at("y").get_to(p.y);
    // 简化写法（但无异常，键不存在会赋值默认值）：
    // p.x = j["x"]; p.y = j["y"];
}


// ================================

enum class AgvStatus {
    UNKNOWN = -1, // 未知/离线/不存在  
    IDLE = 0,     // 空闲
    MOVING,       // 移动中
    PAUSED,       // 暂停
    ERROR,        // 故障
    CHARGING      // 充电中
};

// Enum 序列化 
NLOHMANN_JSON_SERIALIZE_ENUM( AgvStatus, {
    {AgvStatus::IDLE,      "IDLE"    },
    {AgvStatus::MOVING,    "MOVING"  },
    {AgvStatus::PAUSED,    "PAUSED"  },
    {AgvStatus::ERROR,     "ERROR"   },
    {AgvStatus::CHARGING,  "CHARGING"}
})

// ================================
enum class ActionType {
    NONE = 0,
    LIFT_UP,
    PUT_DOWN,
    CHARGE
};

// ActionType 序列化 
NLOHMANN_JSON_SERIALIZE_ENUM(ActionType, {
    {ActionType::NONE,     "NONE"    },          
    {ActionType::LIFT_UP,  "LIFT_UP" },    
    {ActionType::PUT_DOWN, "PUT_DOWN"},  
    {ActionType::CHARGE,   "CHARGE"  }       
})



struct AgvInfo {
    // ---静态身份信息 ：来自 Login
    AgvId uid = -1;  // 必须存入。这是 map 的 Key，也是车的身份证

    std::string version;

    // ---动态物理信息 ：来自 Heartbeat / TaskReport
    Point currentPos = {0, 0}; // 核心数据。决定了它会不会撞车，离任务有多远
    double battery = 100.0; // 核心数据。决定了是否需要生成“去充电”的任务

    // --- 逻辑状态信息 ：··    
    AgvStatus status = AgvStatus::IDLE; // 核心数据。决定了它能不能接单
    std::string currentTaskId;  // 当前正在执行的任务ID (空字符串表示没任务)
    double taskProgress = 0.0;  // 任务进度

    // --- 运维保活信息
    int64_t lastHeartbeatTime = 0;  // 核心数据。用来计算“最后一次心跳时间”，判断是否断连 毫秒级
};


// ==========================================
// 2. 业务 Payload (对应 MsgType)
// ==========================================

// [MsgType::LOGIN_REQ] 登录请求
struct LoginRequest {
    AgvId agvId;
    std::string password;
    std::string version;
    Point initialPos = {0, 0};  // 初始位置
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LoginRequest, agvId, password, version, initialPos)

// [MsgType::LOGIN_RESP] 登录响应
struct LoginResponse {
    bool success;
    std::string token;  // AGV 登录成功后，服务器颁发给 AGV 的「身份凭证」
    std::string message; // 错误提示
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LoginResponse, success, token, message)

// [MsgType::HEARTBEAT] 心跳包
struct Heartbeat {
    AgvId agvId;
    AgvStatus status;
    Point currentPos;
    double battery; // 0.0 - 100.0
    int64_t timestamp; 

};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Heartbeat, agvId, status, currentPos, battery, timestamp)

// [MsgType::TASK_REQUEST] 任务下发
struct TaskRequest {
    std::string taskId;
    AgvId targetAgvId;
    Point targetPos;
    ActionType targetAct;  // action, 到了目的地干什么
    int priority;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TaskRequest, taskId, targetAgvId, targetPos, targetAct, priority)


// [MsgType::TASK_REPORT] 任务状态上报
struct TaskReport {
    std::string taskId;
    AgvId agvId;
    AgvStatus status;
    Point currentPos;
    double progress; // 0.0-1.0 任务进展
    int32_t refSeq = 0; // 关联序列号：表示对 Server 哪条指令的回复; 如果是主动上报（非回复），填 0 
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TaskReport, taskId, agvId, status, currentPos, progress, refSeq)

// [MsgType::PATH_REQ] 寻路请求
struct PathRequest {
    int mapId;   // 地图ID,暂时用不到
    Point start;
    Point end;
    bool allowReplan;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PathRequest, mapId, start, end, allowReplan)

// [MsgType::PATH_RESP] 寻路响应
struct PathResponse {
    bool success;
    std::vector<Point> pathPoints;
    std::string failReason;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PathResponse, success, pathPoints, failReason)

}
}