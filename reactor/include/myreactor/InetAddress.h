#pragma once
#include <netinet/in.h>
#include <string>

/*
1. 成员变量（存储数据）
核心成员应该是隐藏的
2. 构造函数（灵活初始化）
一个好的类应该提供多种初始化方式(用户输入的ip和port；accept返回的结构体)
3.核心方法（服务网络 API）
这是封装的精髓，用于提供给其他类（如 Socket 类）调用：
4. 辅助方法（用户友好）
用于调试或日志输出：4

InetAddress 类的核心职责是：
存储并表示一个 IP 地址和端口号。
提供各种格式的地址信息供网络库调用
*/

namespace myreactor{

class InetAddress{
private:
    //用于存储IPv4地址和端口的底层结构体
    struct sockaddr_in addr_;
public:
    // 构造与析构函数 1.listenfd时候输入的ip和port 2.accpet返回的结构体
    InetAddress();

    InetAddress(const std::string &ip,uint16_t port);

    InetAddress(const struct sockaddr_in addr);

    ~InetAddress() = default;

    /*
    // 这种情况下 const 是有意义的：表示调用者不能通过引用修改内部数据
        const std::string& getName() const; 
    // 这种情况下 const 是无意义的：因为返回的是副本
        const int getId() const; -> int getId() const;
    */

    // 核心方法 1.返回sockaddr结构体指针 2.返回系统调用(bind)需要的地址长度
    const struct sockaddr *getSockAddr() const;

    socklen_t len() const;

    // 辅助方法 1.返回字符串格式的ip 2.返回uint16_t的port 3.Socket::accept(&clientaddr) 中对addr的设置
    const char* ip() const;

    uint16_t port() const;

    void setaddr(sockaddr_in peeraddr);

};

}