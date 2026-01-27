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

/*
Header 负责传输，Body 负责关联
为了既保证消息流不断，又能匹配请求，需要两个字段：
    MsgHead.seq: “这是 AGV 发出的第 xx 个包”。
    MsgBody.refSeq (或 ackSeq): “这是对 Server 第 yy 号包的回应”。
*/

// ==========================================
// 1. 消息包头 (MsgHead) - 解决TCP协议的粘包问题、序列号
// ==========================================
struct MsgHead{
    int32_t len;  // 4字节 的包体长度,不包含包头
    int32_t type; // 4字节int 型 的消息类型 (对应MsgType)
    int32_t seq;  // 序列号 ： 用于异步请求-响应匹配
};

// 定义包体最大长度 64KB ，防止恶意工艺导致内存爆炸
/*
把 “运行时只读变量” 改成 “编译期常量(只读变量)”，同时把 “通用整数类型” 改成 “语义匹配的长度类型”—— 既提升了性能 / 安全性，又符合 C++ 标准的类型语义
    constexpr 是 C++11 引入的关键字，全称 “constant expression”（常量表达式），表示：这个变量的值在「编译阶段」就能完全确定，而非运行阶段。
    1. 支持所有编译期常量场景；2. 编译器可优化，性能更高
*/
// const int MAX_MSG_BODY_LEN = 65536; 
// 1. 数组长度 / 模板参数等 “必须编译期确定” 的场景可能不合法；2. 编译器无法做常量折叠优化
// 修正为： 定义定长数组很合适
constexpr int32_t MAX_MSG_BODY_LEN = 65536;

// ==========================================
// 2. 消息对象 (AgvMessage) - 核心载体
// ==========================================
class AgvMessage{
public:
    MsgHead head;                   // 协议头
    char body[MAX_MSG_BODY_LEN];    // 协议体 (定长数组，配合内存池效率最高)

    /* Thread Local Object Pool
    1. inline: 允许在头文件中直接初始化 (C++17 特性)，不用去 .cpp 里再写一遍 AgvMessage::pool ...
    2. static: 类的所有实例共享，保证整个程序运行期间，这个 pool 对象一直存在。不会随着函数作用域结束而销毁,且每次调用时，static 会保留上次调用结束时的值
    3. thread_local: 保证每个线程 (SubReactor)都有自己独立的一份 pool, 从而使得： 
        线程 A 操作 pool_A , 线程 B 操作 pool_B , 完全无锁 (Lock-Free)
    */
    /*
    C++ 标准规定，thread_local 变量是在该线程中“第一次使用它（ODR-use）”之前的那一刻初始化的。
    这意味着：虽然它叫 thread_local，但它并不是线程一启动（Thread Start）就立马创建的，而是延迟到你要用它的那一毫秒才创建。
    */
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
    // data： AgvCodec 把 AgvStruct 业务结构体序列化后的「JSON 字符串」（文本形式的字节序列），是 “装信封的业务内容”
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