#pragma once
#include <myreactor/InetAddress.h>
#include <myreactor/Timestamp.h>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <myreactor/Buffer.h>

namespace myreactor{

class EventLoop;
class Socket;
class Channel;

class Connection : public std::enable_shared_from_this<Connection>{
private:
    EventLoop* loop_;
    std::unique_ptr<Socket> clientsock_;
    std::unique_ptr<Channel> clientchannel_;
    InetAddress clientaddr_;

    Buffer inputBuffer_;
    Buffer outputBuffer_;

    Timestamp lastTime_;

    // 新增：配合prase错误关闭连接
    bool isDisconnecting_;

    using spConnection = std::shared_ptr<Connection>;

    using CB = std::function<void(const spConnection&)>;
    CB closecb_;
    CB errorcb_;
    CB sendCompletecb_;
    using RCB = std::function<void(const spConnection&, Buffer*)>;
    RCB readcb_;

    void handleRead();
    void handleClose();
    void handleError();
    void handleWrite();

    // 内部发送逻辑
    void sendInLoopCore(const char*, size_t);

    // 业务层对象的指针，强引用保活
    std::shared_ptr<void> context_;


public:
    Connection(EventLoop* loop, std::unique_ptr<Socket> clientsock, const InetAddress& clientaddr);

    ~Connection();

    // 上下文 Context 接口 ： 无锁优化  ===============================
    inline void setContext(const std::shared_ptr<void>& context) {
        context_ = context;
    }

    template<typename T>
    std::shared_ptr<T> getContext() const {
        return std::static_pointer_cast<T>(context_);
    }

    void clearContext() {
        context_.reset();
    }
    // 上下文 Context 接口 ： 无锁优化  ===============================

    int fd() const;
    std::string ip() const;
    uint16_t port() const;

    // 用于在TcpServer::newconnection中,对象创建后立即建立 Channel 与 Connection 的弱绑定
    void connectEstablished();

    void setclosecb(const CB& cb);
    void seterrorcb(const CB& cb);
    void setreadcb(const RCB& cb);
    void setsendCompletecb(const CB& cb);

    void send(const char* data, size_t size);
    // AgvServer 拼好 Buffer 后调用这个
    void send(Buffer* buf);

    // val 表示时间间隔，单位为秒，为了增加紧密度，设置为double
    bool istimeout(Timestamp now, double val) const;

    void forceClose();
};

}