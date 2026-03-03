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