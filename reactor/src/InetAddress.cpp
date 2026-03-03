#include<myreactor/InetAddress.h>
#include <arpa/inet.h>


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