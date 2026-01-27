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
    // using TOCB = std::function<void(EventLoop*)>; // epolltimeout 只是“做事”（打印日志、处理超时），而不去“销毁”或“长期持有”这个 loop 指针，那么裸指针 EventLoop* 就是最完美的桥梁，它完美兼容了两种不同的管理方式。

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
    // void epolltimeout(EventLoop* loop);
    /*
    TcpServer利用Eventloop类的定时器功能，封装了一个定时清理空闲连接的工具，利用了下层的定时机制，补充判断是否空闲的逻辑，从而决定是否清理连接的操作
    因此命名上，下层的EventLoop类保持handleTimer，而上层TcpServer封装后的功能函数名则有功能“cleanIdle”的意味
    */

    // 底层定时器回调，由 EventLoop 驱动
    void onTimer();  //组装上层的超时业务以及本层的超时清理业务
    // 核心业务逻辑：清理空闲连接
    void cleanIdleConnections();

    
    /*
    TcpServer 在onmessage中完成业务逻辑处理（比如给客户端消息加reply:前缀）后，需要把处理后的结果发送给客户端。但 TcpServer不具备直接发送数据的能力（因为它不持有 socket、不管理缓冲区、不处理非阻塞 IO），必须通过 Connection 提供的send()方法，将 “业务处理后的结果” 交给 Connection 层，由 Connection 层完成网络层面的发送操作（追加缓冲区、注册写事件、触发handleWrite发送）。
    */

    int16_t port_;

public:
    TcpServer(const std::string &ip, const uint16_t& port, int threadnum=2);
    ~TcpServer();

    inline uint16_t port() {return port_;}
    /*
    unique_ptr 与 析构函数的位置
    你使用了 std::unique_ptr<Acceptor> acceptor_; 配合前置声明。 这要求 TcpServer 的析构函数必须在 .cpp 文件中定义（哪怕是空的）。
    原因：unique_ptr 在析构时会调用 delete acceptor_，这会触发 sizeof(Acceptor) 检查。如果在 .h 文件里写了 ~TcpServer() {}，此时编译器还不知道 Acceptor 的完整定义（只看到了前置声明），会报错 incomplete type。
    */

    void start();
    void stop();

    // 添加访问接口来访问私有成员！
    // Connection* getConnection(int connfd) const;

    // 这里的 sendToClient 可以废弃了，因为上层 EchoServer 直接拿到了 spConnection，可以直接调 conn->send()
    // bool sendToClient(int connfd, const char* data, size_t size);

    void setTimeOut(int timeout);

    void setnewconncb(const CB& fn);
    void setclosecb(const CB& fn);
    void seterrorcb(const CB& fn);
    void setsendcompcb(const CB& fn);
    void setonmesscb(const MCB& fn);
    void setconntimeoutcb(const CB& fn);

    /*
    正确的思考思路是：
    1.先看终点：TcpServer 需要什么数据才能管理好连接？
    2.看供给侧：Acceptor 能不能拿到这些数据？
    3.定接口：将两者的回调签名统一为 void(int, const InetAddress&)。
    4.写实现：在 Acceptor::handleRead 中完成从“无参触发”到“有参回调”的桥接。
    确定参数的正确思路不是看底层有什么，而是看 TcpServer 为了完成业务逻辑，到底需要什么？
    第一步：明确 TcpServer 的职责
        newconnection 的核心任务是：创建一个新的 Connection 对象并管理它。 要创建一个 Connection，需要：①connfd：新连接的文件描述符，用于通信;②clientAddr：客户端的 IP 和端口，用于日志打印和业务识别。
        结论： 从 TcpServer 的业务需求出发，参数签名至少应该是：void(int connfd, const InetAddress& clientAddr)。
    第二步：检查中层（Acceptor）能否提供这些数据
        Acceptor 执行::accept() 系统调用后（Socket的方法），内核会返回两个值：①函数的返回值：connfd；②填入结构体的：client_addr。
        契约达成： 中层恰好能提供顶层需要的所有数据。因此，Acceptor 内部定义的包装器（NewConnectionCallback）必须与顶层的签名保持一致。
        // Acceptor.h 内部定义的契约（包装器类型）
        using NewConnectionCallback = std::function<void(int, const InetAddress&)>;
    */

    
    inline void setTickcb(const TickCB& cb) {tickcb_ = cb;}


   
};

}