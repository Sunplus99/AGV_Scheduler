#pragma once
#include <functional>
#include <myreactor/InetAddress.h>

namespace myreactor{

class Socket;
class Channel;
class EventLoop;

class Acceptor{
private:
    EventLoop *loop_;
    Socket *servsock_;
    Channel *acceptchannel_;
    bool listening_;
    using NCCB = std::function<void(int,const InetAddress&)>;         
    NCCB newconnectioncb_;
    void handleRead(); 
public:
    Acceptor(EventLoop* loop_, const InetAddress& servaddr);
    ~Acceptor();

    void listen();

    void setnewconnectioncb(const NCCB& cb);   // 赋值包装器函数
};

}