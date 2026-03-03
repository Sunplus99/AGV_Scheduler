#pragma once

#include "utils/Logger.h"
#include "protocol/AgvMessage.h"
#include "model/AgvStructs.h"
#include "myreactor/Buffer.h"
#include "myreactor/Connection.h"
#include <string>
#include <cstring>
#include <functional>
#include <exception>
#include <memory>


/*
【JSON 序列化层】: 业务结构体 ↔ AgvMessage（中间信封）
    packMessage/unpackMessage
【网络编码层】: AgvMessage ↔ Buffer（网络字节流，处理字节序 / 粘包）
    encodeToBuffer/parseFromBuffer
【统一收发层】：整合前两层，提供「一站式」编码 + 发送接口
    send/onMessage
*/


/*
抽象层：固定的编解码流程（不变的核心逻辑）
    编码流程：业务结构体 → JSON 字符串 → AgvMessage 信封 → Buffer（网络字节序） → 发送；
    解码流程：Buffer → AgvMessage 信封 → JSON 字符串 → 业务结构体。
实现层：可变的业务结构体类型（模板适配）
    不同业务场景需要不同的结构体（LoginRequest/TaskRequest/Heartbeat）,没有为每个结构体写单独的,函数，而是用模板 typename T 封装
*/


namespace agv{
namespace codec{

using spConnection = std::shared_ptr<myreactor::Connection>;

// ==========================================
// Part 1: JSON 序列化层 (Struct <-> Message)
// ==========================================

template <typename T>
void packMessage(protocol::AgvMessage& msg, protocol::MsgType type, const T& obj, int32_t seq = 0) {
    json j = obj;
    msg.setRawData(type, j.dump(), seq);
}

template <typename T>
T unpackMessage(const protocol::AgvMessage& msg) {
    // 防御性检查
    if(msg.head.len < 0 || static_cast<size_t>(msg.head.len) > protocol::MAX_MSG_BODY_LEN) {
        throw std::runtime_error("Invalid msg len: " + std::to_string(msg.head.len));
    }

    // 构造 string
    std::string jsonStr(msg.body, msg.head.len);

    
    try{
        // 解析 JSON
        json j = json::parse(jsonStr);
        return j.get<T>();
    } catch (const json::exception& e) {
        // 捕获底层库的异常 -> 包装成带有业务信息的标准异常 -> 抛出
        throw std::runtime_error("JSON Parse Error [Type=" + protocol::msgTypeToString(static_cast<protocol::MsgType>(msg.head.type)) + "]: " + e.what());
    }
}


// ==========================================
// Part 2: 网络编码层 (Message -> Buffer)
// ==========================================

// 将信封里的 Head 和 Body 写入 Buffer 对象，准备发送
inline void encodeToBuffer(const protocol::AgvMessage& msg, myreactor::Buffer& buf) {
    // 包头
    buf.appendInt32(msg.head.len);
    buf.appendInt32(msg.head.type);
    buf.appendInt32(msg.head.seq);

    // 包体
    buf.append(msg.body, msg.head.len);
}

// 定义解析结果状态
enum class ParseResult {
    kSuccess,       // 成功切出一个包
    kNotEnough,     // 半包，继续等
    kError          // 致命错误，协议非法
};  // k : Constant（常量）

inline ParseResult parseFromBuffer(myreactor::Buffer* buf, protocol::AgvMessage& outMsg) {
    // 1.检查包头长度 ：至少有包头
    if(buf->size() < sizeof(protocol::MsgHead))
        return ParseResult::kNotEnough;

    // 2.获取包长但不动数据
    int32_t len = buf->peekInt32();

    // 2.1安全检查 (防恶意大包)
    if(len < 0 || len > protocol::MAX_MSG_BODY_LEN-1) {
        // 严重错误 : 丢弃
        LOG_ERROR("Protocol Error: Invalid msg len=%d, closing connection.", len);
        buf->clear();
        return ParseResult::kError;
    }

    // 2.2检查数据够不够一个完整的包，不够等下次 
    if(buf->size() < sizeof(protocol::MsgHead)+len)
        return ParseResult::kNotEnough;

    // 3.正式读取
    outMsg.head.len  = buf->readInt32();
    outMsg.head.type = buf->readInt32();
    outMsg.head.seq  = buf->readInt32();
    
    if(len > 0) {
        ::memcpy(outMsg.body, buf->data(), len);
        // 封口
        outMsg.body[len] = '\0';
        // 读完即删
        buf->erase(len);
    } else { // len == 0
        outMsg.body[0] = '\0';
    }

    return ParseResult::kSuccess;
}


// ==========================================
// Part 3: 一体化发送接口 send 
// ==========================================

template <typename T>
static void send(const spConnection& conn,
                 protocol::MsgType type, 
                 const T& obj, 
                 int32_t seq = 0)
{
    if(!conn) return;

    /*从对象池申请
      这是一个大对象 (64KB), 防爆栈 （不在栈上）
      使用 new 会触发 AgvMessage::operator new，从 ObjectPool 极速分配
    */ 
    auto* msg = new protocol::AgvMessage();

    packMessage(*msg, type, obj, seq); // *msg解引用：根据地址找到对象

    myreactor::Buffer buf;
    encodeToBuffer(*msg, buf);  // *msg解引用：根据地址找到对象

    // 底层发送
    conn->send(&buf);

    // 归还对象池 ： 触发 AgvMessage::operator delete
    delete msg;

}


// ==========================================
// Part 4: 接收分发器 Dispatcher
// ==========================================



class Dispatcher {
public:
    using MessageCB = std::function<void(const spConnection&, const protocol::AgvMessage&)>;

    // 注册业务回调
    template<typename T, typename UserFunc>
    void registerHandler(protocol::MsgType type, UserFunc&& userFunc){
        callbacks_[type] = [userFunc]
        (const spConnection& conn, const protocol::AgvMessage& msg) 
        {
            try{
                // 获取 内容结构
                T obj = unpackMessage<T>(msg);
                // 调用业务逻辑
                userFunc(conn, obj, msg.head.seq);
            } catch (const std::exception& e){
                LOG_ERROR("Handle error: %s",e.what());
            }
        };

    }


    // 核心分发逻辑(接收与分发一体化)
    void dispatch(const spConnection& conn, myreactor::Buffer* buf) {
        while(true) {
            // auto* msg = new protocol::AgvMessage();
            auto msg = std::make_unique<protocol::AgvMessage>();

            ParseResult result = parseFromBuffer(buf, *msg);

            if (result == ParseResult::kError) {
                // delete msg;  // 有new必须有delete
                conn->forceClose();
                break;
            }

            if (result == ParseResult::kNotEnough) {
                // delete msg;  // 有new必须有delete
                break;    // 半包, 下次再说
            }

            // 查找并执行
            auto it = callbacks_.find(static_cast<protocol::MsgType>(msg->head.type));
            if(it == callbacks_.end()) {
                LOG_WARN("Unknow msg type: %d", msg->head.type);
            } else {
                it->second(conn, *msg);
            }

            // delete msg;
        }
    }

private:
    // 消息类型到业务回调 的 映射表
    std::map<protocol::MsgType, MessageCB> callbacks_;
};



}
}