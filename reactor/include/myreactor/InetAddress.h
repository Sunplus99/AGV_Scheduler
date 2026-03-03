#pragma once
#include <netinet/in.h>
#include <string>

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

    const struct sockaddr *getSockAddr() const;

    socklen_t len() const;

    const char* ip() const;

    uint16_t port() const;

    void setaddr(sockaddr_in peeraddr);

};

}