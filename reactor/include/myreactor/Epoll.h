#pragma once
#include <vector>
#include <sys/epoll.h>

namespace myreactor{

/*
纯粹的 I/O 执行者,Epoll 现在只负责封装系统调用
*/


class Channel;

class Epoll{
private:
    int epollfd_;

    std::vector<struct epoll_event> events_;
    static const int InitEventListSize = 16;
    
    void fillActiveChannels(int numEvents, std::vector<Channel*>* activeChannels) const;

public:
    Epoll();
    ~Epoll();

    
    void update(int op, Channel *ch);  // 更新选项op由Eventloop判断并指示

    // 毫秒（millisecond）
    void poll(int timeoutMs, std::vector<Channel*>* activeChannels);
    
};

}

