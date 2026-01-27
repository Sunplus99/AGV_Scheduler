#pragma once
#include <functional>
#include <myreactor/InetAddress.h>

namespace myreactor{

class Socket;
class Channel;
class EventLoop;

/*
这里隐藏着一个极易忽略的 C++ 初始化陷阱：初始化列表顺序。
在 C++ 中，成员变量的初始化顺序是按照它们在类头文件中声明的顺序进行的，而不是你在初始化列表中写的顺序。
*/
class Acceptor{
private:
    EventLoop *loop_;
    Socket *servsock_;
    Channel *acceptchannel_;
    bool listening_;
    using NCCB = std::function<void(int,const InetAddress&)>;         // 接受TcpServer新连接的包装器
    NCCB newconnectioncb_;
    void handleRead(); // 组装完整新连接的方法，供channel回调,它对下满足 Channel 的 void() 接口;它在函数体内利用 Socket 对象从内核 “吸取” 数据;它对上把吸取到的数据 “喷射” 给 TcpServer 的 newconnection。
public:
    Acceptor(EventLoop* loop_, const InetAddress& servaddr);
    ~Acceptor();

    void listen();

    void setnewconnectioncb(const NCCB& cb);   // 赋值包装器函数
};

}