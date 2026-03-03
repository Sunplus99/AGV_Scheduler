#pragma once
#include <myreactor/EventLoop.h>
#include <unordered_map>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>

namespace myreactor{ // “命名空间重组（Namespace Reopening）”，或者通俗点叫 “打开追加”。

class Acceptor;
class EventLoopThreadPool;
class Connection;
class InetAddress;
class Buffer;


class TcpServer{
private:
    EventLoop mainloop_;
    std::unique_ptr<Acceptor> acceptor_;
    
    std::unique_ptr<EventLoopThreadPool> ioLoopPool_;
    int threadnum_;

    using spConnection = std::shared_ptr<Connection>;
    std::unordered_map<int,spConnection> conns_;
    std::mutex mutex_;

    using CB = std::function<void(const spConnection&)>;
    using MCB = std::function<void(const spConnection&, Buffer*)>;


    CB newconncb_;
    CB closecb_;
    CB errorcb_;
    CB sendcompcb_;
    MCB onmesscb_;
    CB conntimeoutcb_;

    using TickCB = std::function<void()>;
    TickCB tickcb_;

    int timeoutSec_ = -1;

    void newconnection(int connfd, const InetAddress& clientaddr);
    void removeconnection(const spConnection& conn);
    
    // 转发给用户的各种回调
    void closeconnection(const spConnection& conn);
    void errorconnection(const spConnection& conn);
    void readconnection(const spConnection& conn, Buffer* buf);
    void sendcomplete(const spConnection& conn);

    // 底层定时器回调，由 EventLoop 驱动
    void onTimer();  //组装上层的超时业务以及本层的超时清理业务
    // 核心业务逻辑：清理空闲连接
    void cleanIdleConnections();

    int16_t port_;

public:
    TcpServer(const std::string &ip, const uint16_t& port, int threadnum=2);
    ~TcpServer();

    inline uint16_t port() {return port_;}

    void start();
    void stop();

    void setTimeOut(int timeout);

    void setnewconncb(const CB& fn);
    void setclosecb(const CB& fn);
    void seterrorcb(const CB& fn);
    void setsendcompcb(const CB& fn);
    void setonmesscb(const MCB& fn);
    void setconntimeoutcb(const CB& fn);

    inline void setTickcb(const TickCB& cb) {tickcb_ = cb;}


   
};

}