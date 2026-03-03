#include <myreactor/Epoll.h>
#include <myreactor/Channel.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

using namespace myreactor;

Epoll::Epoll() : epollfd_(::epoll_create1(EPOLL_CLOEXEC)), events_(InitEventListSize){
    
}

Epoll::~Epoll() {
    if(epollfd_ >= 0)
        ::close(epollfd_);
}

void Epoll::update(int op, Channel* ch){
    struct epoll_event ev;
    ev.events = ch->events();
    ev.data.ptr = ch;

    if(::epoll_ctl(epollfd_,op,ch->fd(),&ev) == -1){
        if(op == EPOLL_CTL_DEL){
            // 删除出错通常不致命（可能是重复删除），记录日志即可
            perror("epoll_ctl delete error (ignored)");
            return;
        }else{
            perror("epoll_ctl failed");
            exit(-1);
        }
    }
}


void Epoll::poll(int timeoutMs, std::vector<Channel*>* activeChannels) {
    int numEvents = ::epoll_wait(epollfd_, events_.data(), static_cast<int>(events_.size()), timeoutMs);
    
    int savedErrno = errno;

    if(numEvents > 0){
        fillActiveChannels(numEvents, activeChannels);
        // printf("Epoll::poll return %d events.\n", numEvents); // 加这一行
        // 扩容策略：如果这次事件填满了 vector，说明负载很高，下次多给点空间
        if(numEvents == static_cast<int>(events_.size())){
            events_.resize(events_.size()*2);
        }
    } else if(numEvents==0) { 
        return; //超时，什么都不做
    } else{
        if (savedErrno!=EINTR) {
            errno = savedErrno;
            perror("epoll_wait error");
            // 这里是否 exit 取决于策略，通常记录日志即可
        }
    }
}

void Epoll::fillActiveChannels(int numEvents, std::vector<Channel*>* activeChannels) const {
    for(int i = 0; i<numEvents ; ++i){ //只会读取前 numEvents 个数据，后面的脏数据（Dirty Data）我们根本不关心, 【覆盖即清除】
        Channel* ch = static_cast<Channel*>(events_[i].data.ptr);
        ch->setrevents(events_[i].events);
        activeChannels->push_back(ch);
    }
}
