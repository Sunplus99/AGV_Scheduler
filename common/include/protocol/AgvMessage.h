#pragma once 

#include <cstring>
#include <protocol/MsgType.h>
#include <utils/ObjectPool.h>
#include <string>
#include <thread>

/*
只关心如何可靠地打包、分发、回收网络消息，不关心body里的具体业务 : 纯“网络信封”
*/

namespace agv{
namespace protocol{


// ==========================================
// 1. 消息包头 (MsgHead) - 解决TCP协议的粘包问题、序列号
// ==========================================
struct MsgHead{
    int32_t len;  // 4字节 的包体长度,不包含包头
    int32_t type; // 4字节int 型 的消息类型 (对应MsgType)
    int32_t seq;  // 序列号 ： 用于异步请求-响应匹配
};

// 定义包体最大长度 64KB ，防止恶意工艺导致内存爆炸
constexpr int32_t MAX_MSG_BODY_LEN = 65536;

// ==========================================
// 2. 消息对象 (AgvMessage) - 核心载体
// ==========================================
class AgvMessage{
public:
    MsgHead head;                   // 协议头
    char body[MAX_MSG_BODY_LEN];    // 协议体 (定长数组，配合内存池效率最高)

    inline static thread_local ObjectPool<AgvMessage> pool;

    AgvMessage() {
        /*只清空 Head (12字节)，不清空 Body (64KB)
         在高并发下，避免每次 new 都 memset 64KB，极大提升性能*/
        memset(&head, 0, sizeof(head));
        // memset(body, 0, sizeof(body));
        // 防御性编程
        body[0] = '\0';
    }

    // 设置原始数据 
    void setRawData(MsgType type, const std::string& data, int32_t seq = 0) {
        head.type = static_cast<int32_t>(type);
        head.len  = static_cast<int32_t>(data.size());
        head.seq  = seq;

        // 保护性拷贝 ：防止数据溢出
        size_t copyLen = data.size();
        if( copyLen > MAX_MSG_BODY_LEN - 1) { // 预留一个位置给 \0
            copyLen = MAX_MSG_BODY_LEN - 1;
            head.len = static_cast<int32_t>(copyLen);
        }        
        memcpy(body, data.c_str(), copyLen);

        // 手动补 \0，确保作为 C-String 使用时安全 (方便日志打印)
        body[copyLen] = '\0';
    }


    static void* operator new(size_t size) {
        // 从池子中拿一块内存
        return pool.allocate();
    }

    static void operator delete(void* ptr) { // 发生在析构之后
        if(ptr){
            pool.deallocate(static_cast<AgvMessage*>(ptr)); // ObjectPool::deallocate 接收的参数类型是 T* , void* -> T* 需要显示转换
        }
    }
};

}
}