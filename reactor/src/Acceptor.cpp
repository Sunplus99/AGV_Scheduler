#include <myreactor/Acceptor.h>
#include <myreactor/Channel.h>
#include <myreactor/EventLoop.h>
#include <myreactor/Socket.h>
#include <errno.h>

using namespace myreactor;


Acceptor::Acceptor(EventLoop* loop, const InetAddress& addr):
    loop_(loop),
    servsock_(new Socket(Socket::createNonblocking())),
    acceptchannel_(new Channel(loop_, servsock_->fd())),
    listening_(false)
{
    servsock_->setreuseaddr(true);
    servsock_->bind(addr);

    acceptchannel_->setreadcallback(std::bind(&Acceptor::handleRead,this));
}
/*
“我在调试服务器退出逻辑时，发现日志里总是有几条 Bad file descriptor 的 epoll_ctl 删除错误。
经过排查，我发现这是 C++ RAII 资源管理顺序的问题。 比如在 Acceptor 类中，Channel 依赖于 Socket 的文件描述符。
起初我在析构函数中手动 delete 时，错误地先删除了 Socket（导致 FD 关闭），后删除 Channel（导致在已关闭的 FD 上执行 Epoll 删除操作）。
这加深了我对 ‘构造与析构必须对称’ 这一原则的理解：被依赖的资源必须先构造、后析构。 后来我调整了销毁顺序（或者引入了 unique_ptr 利用声明顺序控制析构），完美解决了这个问题，日志也变干净了。”
*/
Acceptor::~Acceptor(){  
    delete acceptchannel_;
    delete servsock_;
}

void Acceptor::handleRead(){ // 核心分发逻辑：桥接底层的 fd 产生与顶层的业务需求
    InetAddress clientaddr;
    int clientfd = -1;

    // 循环accept()：边缘触发（ET）模式下，需一次性处理所有未决连接
    while(true){
        clientfd = servsock_->accept(clientaddr);
        if(clientfd > 0){
            if(newconnectioncb_) 
                newconnectioncb_(clientfd,clientaddr); 
        }else{
            if(errno==EAGAIN || errno==EWOULDBLOCK){
                break; // 队列空了，正常退出
            }else if (errno == EINTR) {
                continue; // 被信号中断，继续尝试 accept
            }else{
                perror("Acceptor::hanleRead() accept failed");
                break;
            }
        }
    }
}

void Acceptor::setnewconnectioncb(const NCCB& cb) {
    newconnectioncb_ = cb;
}

void Acceptor::listen(){
    if(listening_) return;

    servsock_->listen();
    listening_ = true;
    acceptchannel_->useet();
    acceptchannel_->enablereading();
}