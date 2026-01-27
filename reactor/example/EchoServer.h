#pragma once
#include <myreactor/TcpServer.h>
#include <myreactor/ThreadPool.h>
#include <string>
#include <memory>

/* 
声明命名空间 X 里有类 Y
它们看起来像“重新搞了一个”，实际上是 C++ 允许你随时随地打开同一个命名空间，
往里面塞东西（定义）或者看一眼（声明）。这是 C++ 极具灵活性的体现*/
namespace myreactor{
    class EventLoop;
    class Connection;
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
    void HandleMessage(const spConnection& conn, const std::string& message);
    void HandleSeCom(const spConnection& conn);
    void HandleConnTimeOut(const spConnection& conn);
    /*
    传入loop可以明确当前超时回调属于哪个事件循环，避免操作错误的EventLoop
    传入loop也为后续业务扩展埋下伏笔 —— 比如后续需要在超时后实现 “清理超时空闲连接”“发送心跳包”“动态调整服务配置” 等逻辑时，无需修改回调接口，直接使用loop即可
    */

};