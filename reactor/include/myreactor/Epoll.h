#pragma once
#include <vector>
#include <sys/epoll.h>

namespace myreactor{

/*
纯粹的 I/O 执行者,Epoll 现在只负责封装系统调用
*/

/**
 * @class Epoll
 * @brief 封装 Linux epoll 系统调用，负责管理 epoll 句柄和事件循环。
 * 遵循 RAII 原则管理 epollfd。

 由于 KMaxEvents 是一个不变的常量，并且是所有对象共享的配置值，将其设为 static const 确保了：

内存节省： 无论你创建 1 个还是 100 个 Epoll 对象，KMaxEvents 的内存占用都是一样的，避免了不必要的重复存储。

对象纯净： Epoll 对象的内存大小（sizeof(Epoll)）只取决于它的非静态成员（主要是 epollfd_），保持了对象数据最小化。
 */

class Channel;

class Epoll{
private:
    int epollfd_;
    // static const int MaxEvents = 100;
    // struct epoll_event events_[MaxEvents];
    // 使用vector而不是定长数组，方便扩容
    std::vector<struct epoll_event> events_;
    static const int InitEventListSize = 16;
    // static int createEpollfd();

    /*
    职责解耦：把「事件填充」和「核心 poll 逻辑」拆分开，poll函数只负责 “等待事件、处理错误、扩容”，fillActiveChannels只负责 “填充活跃事件”，后续扩展事件处理逻辑时，只需修改fillActiveChannels，无需改动poll的核心逻辑，避免 “牵一发而动全身”。
    */
    void fillActiveChannels(int numEvents, std::vector<Channel*>* activeChannels) const;

public:
    Epoll();
    ~Epoll();

    // 核心方法 1.epoll_ctl()系统调用，供Eventloop使用 2.epoll_wait()，既返回事件数量，又更新events_成员，供Eventloop使用
    void update(int op, Channel *ch);  // 更新选项op由Eventloop判断并指示

    // int wait(int timeout=-1); 
    // 毫秒（millisecond）
    void poll(int timeoutMs, std::vector<Channel*>* activeChannels);
    
    // struct epoll_event* getReadyEvents();
};

}

/*
KMaxEvents 为什么作为成员变量？
将 KMaxEvents 定义为类内部的 静态常量成员 (static const int KMaxEvents = 100;) 是更推荐的做法，因为它解决了默认参数方法所带来的设计和维护问题。

❌ 为什么不推荐作为 wait 的默认参数？
将 MaxEvents 作为 wait 的默认参数 (wait(int MaxEvents = 100, int timeout = -1)) 会导致以下问题：

 这个参数的本质不是用户可随意修改的配置，而是一次 epoll_wait 调用中数组的物理大小限制。将其暴露为用户参数，容易误导调用者认为可以在运行时动态调整这个核心限制。多数情况下，用户调用 wait 时只关心 timeout。他们被迫要记住或忽略第一个参数，使 API 看起来更复杂。

在 Epoll.cpp 内部实现 wait 方法时，可以方便地用它来定义临时数组或 vector 的大小，确保了 内部实现的一致性。

*/