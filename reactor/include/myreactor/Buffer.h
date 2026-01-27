#pragma once
#include <string>
#include <vector>
#include <sys/types.h> // 【新增】为了 ssize_t，保证移植性

namespace myreactor{

/*
    加入 append(void*) (写在 .h 为了内联)。
    加入 appendInt32 / appendInt16 (通用网络工具)。
    加入 append(string) (通用 C++ 工具)。
*/

class Buffer{
private:
    // 头部预留空间
    static const size_t kCheapPrepend = 16; 
    static const size_t kInitialSize = 1024;
    /*
    设置8字节的kCheapPrepend 空间:
        ① 可以通过移动 readerIndex_ 指针直接在原内存段前方填入协议头，避免了昂贵的消息整体挪动开销，将头部追加操作的复杂度从 $O(N)$ 降低到了 $O(1)$ 
        ② 8 字节：足够容纳一个 64 位整数（int64_t），能满足绝大多数网络协议头部的需求，此外也是为了内存对齐，即8 字节是 64 位机器上标准的字长，有利于 CPU 访问内存的效率。
    */
    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;

    char* beginWrite();
    char* beginRead(); // 供内部的

    size_t readableBytes() const;
    size_t writableBytes() const;

    void makeSpace(size_t len); // 扩容或整理内存
    void ensureWritableBytes(size_t len);
    void ensureReadableBytes(size_t len) const;

    void retrieve(size_t len); // 删，对内提供
    void retrieveAll();
   
public:
    explicit Buffer(size_t initialSize = kInitialSize);
    ~Buffer() = default;

    // 从 socket 读数据
    ssize_t readFd(int fd, int* savedErrno);

    // ===========================================
    // 写入接口 (Append) 
    // ===========================================

    /*
    Buffer::appendInt32/hton :
    	gvMessage 的包头（int32_t len/type/seq）;解决大小端，保证跨机器传输
    Buffer::append:
    	AgvMessage 的 body（JSON 字符串）
    */

    // 1. 【基础核心】 (The Kernel)
    // 底层写入（移动writerIndex_） ; 不关心内容，原样拷贝
    void append(const void* data, size_t len);

    // 2. 【序列化写入】 (The Serializer)
    void appendInt16(int16_t x); // 2字节颗粒度
    void appendInt32(int32_t x); // 4字节颗粒度
    void appendInt64(int64_t x); // 8字节颗粒度

    // 3. 【适配器】
    void append(const std::string& str) { // string 适配
        append(str.data(), str.size());
    }

    // ===========================================
    // 头部插入接口 (Prepend) - 利用 kCheapPrepend
    // ===========================================
    
    // 1. 【基础核心】 (The Kernel)
    // 底层插入（移动readerIndex_） ; 不关心内容，原样拷贝
    void prepend(const void* data, size_t len);

    // 2. 【序列化插入】 (The Serializer)
    void prependInt16(int16_t x); // 2字节颗粒度
    void prependInt32(int32_t x); // 4字节颗粒度
    void prependInt64(int64_t x); // 8字节颗粒度

    // ===========================================
    // 读与读取接口 (Peek & erase) 
    // ===========================================

    // 1.  【反序列化读】
    int16_t peekInt16() const;
    int32_t peekInt32() const;
    int64_t peekInt64() const;

    // 【基础删】
    void erase(size_t len); 

    // 2.1 【便捷读取】
    std::string readAsString(size_t len);
    std::string readAllAsString();

    // 2.2 【反序列化读取】
    int16_t readInt16();
    int32_t readInt32();
    int64_t readInt64();

    // ===========================================
    // 仿STL常用接口 
    // ===========================================

    size_t size() const;
    const char* data() const; // 对内/外提供可读数据的起始指针
    bool isempty() const;
    void clear(); // 对外提供的清空
};

}

