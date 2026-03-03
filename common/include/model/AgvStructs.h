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

/*
当面试官问你：“你这些协议字段是怎么定义的？参考了什么标准吗？”

 “在设计通信协议时，我主要参考了 VDA 5050 协议的设计思想，并结合项目的实际需求进行了简化。
    状态同步方面：我参考了 VDA 5050 的 State 主题，保留了调度最核心的 位置(Position)、电量(Battery) 和 运行状态(Status)，去掉了工业场景中复杂的负载和车轮数据，以减轻网络负载。
    任务流转方面：我采用了标准的 Request-Report（请求-上报） 闭环模型。Server 下发 TaskID 和目标点，AGV 阶段性上报进度（Progress）和状态，确保调度系统能实时监控任务执行情况。
    安全性设计：借鉴了工业界的心跳保活机制，我在协议中加入了 Heartbeat 包和 timestamp，配合 TCP 的 KeepAlive，确保系统能毫秒级感知 AGV 的掉线或故障。”
*/

/*
定义业务的核心实体和规则，不关心网络传输 : 纯“业务数据模型”
定义所有业务场景的数据结构（比如登录请求、任务下发），并实现和 JSON 的映射 —— 只关心业务语义，不碰传输细节。
*/

/*
Buffer 的序列化是「传输层 / 字节级序列化」，解决 “基本数据类型在网络传输中的字节序、内存布局问题”；
JSON 的序列化是「应用层 / 结构化数据级序列化」，解决 “复杂业务结构体的跨语言、易解析的字符串表示问题”。
*/

/*非侵入式（NON-INTRUSIVE）
侵入式：需要修改结构体 / 类的定义（比如继承特定基类、加成员函数）；
非侵入式：无需修改结构体核心定义（只加一行宏），不破坏原有结构，是工业级代码的首选（你的代码里就是这种）
*/
/*
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE：
    为结构体生成 “非侵入式” 序列化函数，无需修改结构体核心逻辑
    NON_INTRUSIVE (非侵入式)：必须写在 结构体外部（通常紧跟在 }; 之后）。
    INTRUSIVE (侵入式)：必须写在 结构体内部。
NLOHMANN_JSON_SERIALIZE_ENUM：
    为枚举定义 “值↔字符串” 映射，替代默认的 “值↔数字”，让 JSON 更易读。
    枚举宏：参数是「枚举名 + 映射对」，写在枚举外部。
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

/*
j.get_to(T& obj):输出参数方式写入解析结果,无返回值，直接修改传入的引用对象，语义是 “解析并写入”
j.get<T>()：返回值方式获取解析结果，返回值为 T 类型对象，会创建新对象并返回，语义是 “获取并返回”
*/
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


/*
 AgvInfo 是 AGV 在服务器端的全量状态镜像 , 无需 JSON 序列化 ; 
 维护（写）只有 WorldManager 
    所有的网络消息（登录、心跳、任务上报）解析后，都要喂给 WorldManager 去更新 AgvInfo
然只有 WorldManager 能写，但读它的人很多（通过 WorldManager::GetAllAgvs 或查询接口）：
    TaskManager（调度器）：
        “我要派个任务，哪辆车是 IDLE 状态？” -> 读 status。
        “这辆车电量只有 10% 了，不能派远程任务。” -> 读 battery。
        “我要去 (10, 10)，哪辆车离得最近？” -> 读 currentPos。
    HttpServer（前端监控）：
        老板要看大屏，屏幕上那些移动的小图标，就是每秒从 AgvInfo 读出来
*/
struct AgvInfo {
    // ---静态身份信息 ：来自 Login
    AgvId uid = -1;  // 必须存入。这是 map 的 Key，也是车的身份证
    /*version：建议存入。
    调度时很有用。比如有旧款车 V1.0 和新款车 V2.0。V1.0 只能载重 100kg，V2.0 能载重 500kg。调度器看到 version 就能决定派谁。
    */
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

/*Payload（有效载荷）：通信领域的核心概念
在一次通信中，「真正有业务意义的数据」—— 区别于 “协议头（Header）”“校验位” 等辅助信息。
*/
// [MsgType::LOGIN_REQ] 登录请求
struct LoginRequest {
    AgvId agvId;
    std::string password;
    std::string version;
    Point initialPos = {0, 0};  // 初始位置
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LoginRequest, agvId, password, version, initialPos)

/*
目前是基于 TCP 长连接的内存状态维护会话（isLogin_），Token 主要是为了预留给未来做断线快速重连或者对接 HTTP 管理端使用的。
*/
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
    /*
    Server.now - msg.timestamp 来监控 网络延迟 (Latency)
    如果这个差值突然变大，说明网络卡顿。保留它是为了可观测性
    */ 
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

/*
Header 负责传输，Body 负责关联
为了既保证消息流不断，又能匹配请求，需要两个字段：
    MsgHead.seq: “这是 AGV 发出的第 xx 个包”。
    MsgBody.refSeq (或 ackSeq): “这是对 Server 第 yy 号包的回应”。
*/
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