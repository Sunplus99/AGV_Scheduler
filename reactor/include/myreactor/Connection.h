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

    // 如果直接在 .h 里写 = default，编译可能会报错！
    /*
    ~Connection 析构时需要销毁 unique_ptr，而销毁 unique_ptr 需要调用 Socket 的析构函数。如果此时编译器只看到了 class Socket; 而不知道 Socket 的具体大小和定义（Incomplete Type），它就无法生成删除代码，从而报错（通常是 static_assert 失败，提示 sizeof 无法应用）
    */
    // ~Connection() = default;  X
    // 处理方式 : 在头文件中仅声明,源文件default
    ~Connection();

    /*
    std::static_pointer_cast 是 C++ 标准库为 shared_ptr 提供的静态类型转换工具，对应原生指针的 static_cast：编译期完成转换，无运行时类型检查，速度远快于 dynamic_pointer_cast；但安全性依赖业务逻辑保证类型兼容，若类型不匹配会导致未定义行为。在能确保类型正确的场景（如我的项目中 getContext 方法），用它既保证 shared_ptr 引用计数安全，又能提升性能。
    */
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

    // 兼容旧接口，但在 AgvServer 里我们通常不直接用这个发业务包，除非是简单的心跳
    void send(const char* data, size_t size);
    // AgvServer 拼好 Buffer 后调用这个
    void send(Buffer* buf);

    // val 表示时间间隔，单位为秒，为了增加紧密度，设置为double
    bool istimeout(Timestamp now, double val) const;

    void forceClose();
};

}