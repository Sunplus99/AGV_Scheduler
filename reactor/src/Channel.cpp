#include <myreactor/Channel.h>
#include <myreactor/EventLoop.h>
#include <stdio.h>
#include <sys/epoll.h>

using namespace myreactor;

Channel::Channel(EventLoop* loop,int fd):loop_(loop),fd_(fd) {}      // 构造函数。

Channel::~Channel()                           // 析构函数。 
{
    // printf("Channel[fd=%d] 已销毁(Epoll事件已移除)\n",fd_);

    if(loop_) loop_->removechannel(this);

    readcallback_ = nullptr;
    closecallback_ = nullptr;
    errorcallback_ = nullptr;
}

void Channel::tie(const std::shared_ptr<void>& obj){
    tie_ = obj;
    tied_ = true;
}

void Channel::update() {loop_->updatechannel(this);}

int Channel::fd() const {return fd_;}

uint32_t Channel::events() const {return events_;}

uint32_t Channel::revents() const {return revents_;} 

bool Channel::inepoll() const {return inepoll_;}

bool Channel::iswriting() const {return events_ & EPOLLOUT;}

void Channel::setinepoll(bool on) {inepoll_ = on;}

void Channel::useet(){
    events_ |= EPOLLET;
}

void Channel::enablereading()                     // 让epoll_wait()监视fd_的读事件。
{
    events_|=EPOLLIN;
    update();
}

void Channel::disablereading(){
    events_&=~EPOLLIN;
    update();
}

void Channel::enablewriting(){
    events_|=EPOLLOUT;
    // iswriting_ = true;
    update();
}

void Channel::disablewriting(){
    events_&=~EPOLLOUT;
    // iswriting_ = false;
    update();
}

void Channel::setrevents(uint32_t ev) {revents_=ev;}

void Channel::setreadcallback(ECB fn) {readcallback_=fn;}

void Channel::setclosecallback(ECB fn) {closecallback_ = fn;}
    
void Channel::seterrorcallback(ECB fn) {errorcallback_ = fn;}

void Channel::setwritecallback(ECB fn) {writecallback_ = fn;}


void Channel::handleEvent(){
    if(tied_){ // 把Conntion对象分流出来
        std::shared_ptr<void> guard = tie_.lock();
        if(guard) {
            handleEventwithGuard();
        } else{
            // 对象已销毁，不再执行回调
            printf("Channel[fd%d] 所属对象已销毁，停止回调\n",fd_);
        }
    }else{ // Acceptor对象
        handleEventwithGuard();
    }
}

void Channel::handleEventwithGuard(){
    // 加上这行日志，看看 revents_ 到底是多少
    // printf("Channel::handleEvent revents_ = %d, fd = %d\n", revents_, fd_);
    if((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)){
        if (closecallback_) closecallback_();
    }

    if(revents_ & EPOLLRDHUP) {
        if (closecallback_) closecallback_();
    }

    if(revents_ & (EPOLLIN | EPOLLPRI)){
        if (readcallback_) readcallback_();
        // else printf("Error: readCallback_ is null!\n"); // 防止回调没注册
    }

    if(revents_ & EPOLLOUT) {
        if (writecallback_) writecallback_();
    }

    if(revents_ & EPOLLERR) {
        if (errorcallback_) errorcallback_();
    }
}