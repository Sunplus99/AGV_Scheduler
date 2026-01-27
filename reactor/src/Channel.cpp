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

// 如果正在监听写事件（iswriting()），说明上一轮数据还没发完
/*
不要引入 bool iswriting_ 成员变量！ 维护两个状态变量（events_ 和 iswriting_）容易导致状态不一致的 Bug。 真理只有一个：events_ 才是传递给 epoll 的唯一凭证。所以 iswriting() 应该通过判断 events_ 中是否包含 EPOLLOUT 标志位来实现。
*/
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

/*Channel 里的 weak_ptr (Tie)
场景：父子关系（Parent-Child）。
持有关系：Connection (父) -> std::unique_ptr<Channel> (子)。
执行流：Channel 触发事件 -> 回调 Connection 的函数 -> Connection 可能在回调里把自己销毁了。
危险：如果在执行 handleRead 的过程中，Connection 被销毁了，Channel 作为一个成员变量也会随之销毁，程序直接崩。
解法：Channel 用 weak_ptr "绑" 住 Connection。在执行回调前，先 lock() 强行给 Connection 续命 +1秒。等回调执行完，引用计数归还，大家一起安详离去。
关键词：保活（Keep Alive）。
*/

void Channel::handleEvent(){
    if(tied_){ // 把Conntion对象分流出来
        std::shared_ptr<void> guard = tie_.lock();
        if(guard) {
            // 只有当对象（Connection）还活着，且weak_ptr 提升为 shared_ptr成功,才会执行 事件处理，且在执行期间参与shared计数，即使外部决定要销毁Connection对象，也会因为互锁关系“续命”，直到处理函数执行完成
            handleEventwithGuard();
        } else{
            // 对象已销毁，不再执行回调
            printf("Channel[fd%d] 所属对象已销毁，停止回调\n",fd_);
        }
    }else{ // Acceptor对象
        handleEventwithGuard();
    }
}

/*
EPOLLHUP与EPOLLIN，进入readcallback_分支，while(true){：：read}直到读完会返回-1，且errno不是EAGAIN或EWOULDBLOCK,而是ECONNRESET,最终会被 Connection::line 219处理
*/
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