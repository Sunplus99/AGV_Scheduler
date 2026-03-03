#include <myreactor/Connection.h>
#include <cstdio>
#include <cerrno>
#include <unistd.h>
#include <cstring>
#include <myreactor/EventLoop.h>
#include <myreactor/Socket.h>
#include <myreactor/Channel.h>

using namespace myreactor;


Connection::Connection(EventLoop *loop, std::unique_ptr<Socket> clientsock, const InetAddress& clientaddr)
    : loop_(loop), 
    clientsock_(std::move(clientsock)), // 调用unique_ptr的移动构造函数
    clientaddr_(clientaddr),
    lastTime_(Timestamp::now()),
    isDisconnecting_(false)    
{
    clientchannel_ = std::make_unique<Channel>(loop_, clientsock_->fd()); // 一步到位：分配内存 + 调用构造 + 包装成智能指针

    clientchannel_->setreadcallback(std::bind(&Connection::handleRead, this));
    clientchannel_->setclosecallback(std::bind(&Connection::handleClose,this));
    clientchannel_->seterrorcallback(std::bind(&Connection::handleError,this));
    clientchannel_->setwritecallback(std::bind(&Connection::handleWrite,this));

    clientchannel_->useet();

}

Connection::~Connection() = default;
    // printf("Connection[fd=%d, ip=%s, port=%d] 已销毁\n", fd(), ip().c_str(), port());

int Connection::fd() const {
    // 优先从 Socket 获取，它是资源的持有者
    return clientsock_ ? clientsock_->fd() : -1;
}


std::string Connection::ip() const{
    return clientaddr_.ip();
}

uint16_t Connection::port() const {
    return clientaddr_.port();
}

/*

*/
void Connection::connectEstablished(){
    clientchannel_->tie(shared_from_this());
    clientchannel_->enablereading();
}

void Connection::setclosecb(const CB& cb) {
    closecb_ = cb;
}


void Connection::seterrorcb(const CB& cb) {
    errorcb_ = cb;
}

void Connection::setreadcb(const RCB& cb){
    readcb_ = cb;
}

void Connection::setsendCompletecb(const CB& cb){   
    sendCompletecb_ = cb;
}


void Connection::handleRead(){
    int savedErrno = 0;
    bool error = false;

    // ET 模式核心：必须循环读取，直到读空（EAGAIN）或出错
    while(true){ 
        int nread = inputBuffer_.readFd(fd(), &savedErrno);

        if(nread > 0){
            // 读到了数据，继续循环，看看还有没有
        } else if(nread == 0){
            // 在 TCP 套接字场景下，read 返回 0 就是对端关闭连接的信号
            handleClose();
            break;
        } else {
            // nread < 0
            if (savedErrno == EINTR) {
                continue; // 信号中断，不视为错误，继续读
            }
            if (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK) {
                break; // 缓冲区已空，ET 模式下的正常退出
            }
            
            // 3. 真正的系统错误
            perror("Connection::handleRead error");
            error = true;
            break;

        }
    }

    // 新逻辑：只要 Buffer 里有数据，就直接把 Buffer 指针扔给上层
    if(!inputBuffer_.isempty()) {
        lastTime_ = Timestamp::now();

        if(readcb_)
            readcb_(shared_from_this(), &inputBuffer_);
    }

    // 2. 处理完数据后，如果刚才标记了错误，再执行错误处理
    if (error) {
        handleError();
    }

}

// 1. 发送 Raw Data (const char*)
void Connection::send(const char* data, size_t size){
    if(fd() == -1) return;

    // 判断当前线程
    if(loop_->isInLoopThread()){
        sendInLoopCore(data, size);
    } else{
        // 跨线程调用
        // 工作线程 通过 runInLoop 将发送任务交给 IO 线程。
        loop_->runInLoop(  
            [self = shared_from_this(), msg = std::string(data, size)]() { 
                self->sendInLoopCore(msg.data(), msg.size());  // 把函数连同参数打包成可调用对象
            }
        );
    }
}

// 2. 发送 Buffer (Buffer*) ; AgvServer的主力
void Connection::send(Buffer* buf) {
    if(fd() == -1) return;

    if(loop_->isInLoopThread()){
        // 同线程（IO线程）：零拷贝，直接传指针,极致性能，不创建 string
        sendInLoopCore(buf->data(),buf->size());
        // 语义严谨，既然发完了，可以把 buf 清空 (虽然是栈变量)
        buf->clear();
    } else {
        // 跨线程（Worker线程）：拷贝数据到 string 以延长生命周期
        // 必须拷贝到 string 里，利用 RAII 管理生命周期
        loop_->runInLoop(
            [self = shared_from_this(), msg = std::string(buf->data(), buf->size())]() {
                // 到了 IO 线程，把 string 里的数据喂给核心逻辑
                self->sendInLoopCore(msg.data(), msg.size());
            }
        );
    }
}


void Connection::sendInLoopCore(const char* data, size_t size){
    ssize_t nwritten = 0; // 系统调用的返回值,有符号整数，可能为负
    size_t remaining = size;  // 无符号整数
    bool error = false;

    // 1. 尝试直写 Socket
    if (!clientchannel_->iswriting() && outputBuffer_.isempty()){
        nwritten = ::send(fd(), data, size, MSG_NOSIGNAL);
        if(nwritten > 0){
            // 只要成功写入了内核缓冲区，说明连接活着，必须续命！
            lastTime_ = Timestamp::now();
            remaining = size - nwritten;
            if(remaining==0 && sendCompletecb_){
                loop_->queueInLoop(std::bind(sendCompletecb_,shared_from_this()));
            }
        } else{
            nwritten = 0; // 将负数一并归0
            if(errno != EAGAIN && errno != EWOULDBLOCK){
                error = true;
            }
        }
    }

    // 3. 如果没发完，或者发生了 EAGAIN，将剩余数据存入缓冲区并开启监听
    if(!error && remaining > 0){
        outputBuffer_.append(data + nwritten, remaining);
        if(!clientchannel_->iswriting())
            clientchannel_->enablewriting();
    }

    if(error) handleError();
}


void Connection::handleClose(){
    if(closecb_) closecb_(shared_from_this());
}

void Connection::handleError(){
    printf("Connection[fd=%d] 连接异常错误(errno=%d)\n", fd(), errno);

    if(errorcb_) errorcb_(shared_from_this());
}

void Connection::handleWrite(){
    if(outputBuffer_.isempty()){
        clientchannel_->disablewriting();
        return;
    }

    ssize_t nwritten = 0;

    while(!outputBuffer_.isempty()){
        nwritten = ::send(fd(), outputBuffer_.data(), outputBuffer_.size(),MSG_NOSIGNAL);

        if(nwritten > 0) {
            lastTime_ = Timestamp::now();
            outputBuffer_.erase(nwritten);
        }
        else if(nwritten == -1 && (errno==EAGAIN ||errno==EWOULDBLOCK)) return;
        else{
            printf("Connection:handleWrite error!");
            handleError();
            return;
        }
    }

    if(outputBuffer_.isempty()){
        clientchannel_->disablewriting();
        if(sendCompletecb_) 
            sendCompletecb_(shared_from_this());
    }
}

bool Connection::istimeout(Timestamp now, double val) const{
    return now.usSinceEpoch() - lastTime_.usSinceEpoch() > (val*1000000) ;
}

void Connection::forceClose() {
    if(isDisconnecting_) return;

    isDisconnecting_ = true;
    // 2. 只有在 IO 线程中才能真正执行关闭动作（线程安全）
    if (loop_->isInLoopThread()) {
        handleClose(); 
    } else {
        // 如果是 Worker 线程调用的，切回 IO 线程去关
        loop_->queueInLoop(std::bind(&Connection::forceClose, shared_from_this()));
    }
}