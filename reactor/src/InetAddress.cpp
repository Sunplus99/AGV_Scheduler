#include<myreactor/InetAddress.h>
#include <arpa/inet.h>

// class InetAddress{
// private:
//     //用于存储IPv4地址和端口的底层结构体
//     struct sockaddr_in addr_;
// public:
//     // 构造函数 1.listenfd时候输入的ip和port 2.accpet返回的结构体
//     InetAddress(const std::string &ip,uint16_t port);

//     InetAddress(const sockaddr);

//     // 核心方法 1.返回sockaddr结构体指针 2.返回系统调用(bind)需要的地址长度
//     const struct sockaddr *getSockAddr() const;

//     socklen_t len() const;

//     // 辅助方法 1.返回字符串格式的ip 2.返回uint16_t的port
//     std::string ip() const;

//     uint16_t port() const;

// }
using namespace myreactor;

InetAddress::InetAddress() {}

InetAddress::InetAddress(const std::string &ip, uint16_t port){
    addr_.sin_family = AF_INET;
    addr_.sin_addr.s_addr = inet_addr(ip.c_str());
    addr_.sin_port = htons(port);
}

InetAddress::InetAddress(const sockaddr_in addr):addr_(addr){}

// InetAddress::~InetAddress(){}

const struct sockaddr *InetAddress::getSockAddr() const{
    return (struct sockaddr*)&addr_;
}

socklen_t InetAddress::len() const{
    return static_cast<socklen_t>(sizeof(sockaddr));
}

const char* InetAddress::ip() const{
    return inet_ntoa(addr_.sin_addr);
}

uint16_t InetAddress::port() const{
    return ntohs(addr_.sin_port);
}

void InetAddress::setaddr(sockaddr_in peeraddr){
    addr_ = peeraddr;
}