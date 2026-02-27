#pragma once
#include <myreactor/TcpServer.h>
#include <myreactor/ThreadPool.h>
#include <string>
#include <memory>


namespace myreactor{
    class EventLoop;
    class Connection;
    class Buffer;  
}

class EchoServer{
private:
    myreactor::TcpServer tcpserver_;
    myreactor::ThreadPool threadpool_;

    using spConnection = std::shared_ptr<myreactor::Connection>;

public:
    EchoServer(const std::string& ip, const uint16_t port, int iothreadnum=2, int workthreadnum = 2);
    ~EchoServer();

    void Start();
    void Stop();

    void HandleNewConn(const spConnection& conn);
    void HandleClose(const spConnection& conn);
    void HandleError(const spConnection& conn);
    void HandleMessage(const spConnection& conn, myreactor::Buffer* buf); 
    void HandleSeCom(const spConnection& conn);
    void HandleConnTimeOut(const spConnection& conn);
    
};