#pragma once
#include <functional>
#include <memory>

namespace myreactor {

class EventLoop;

/**
 * @class Channel
 * @brief Reactor 模式中事件分发的核心中介者 (Event Demultiplexing Dispatcher)。
 * * Channel 类的设计遵循单一职责原则(SRP)：
 * 1. 绑定并封装一个文件描述符(FD)的事件状态。
 * 2. 充当 FD 事件的代理，将 I/O 事件安全、高效地路由到上层业务逻辑(回调函数)。
 * * 设计哲学：
 * Channel 不关心底层 I/O 机制(Epoll)的实现细节，所有 I/O 同步请求都通过 EventLoop 转发。
 */

 /*
 fd（内核资源） ←→ Channel（事件回调） ←→ EventLoop（事件调度） ←→ Epoll（epoll封装）
 */

class Channel{
private:
    EventLoop* loop_ = nullptr;
    const int fd_ = -1;

    uint32_t events_ = 0;
    uint32_t revents_ = 0;

    bool inepoll_ = false;
    // bool iswriting_ = false;
    
    // 弱引用指针，防止悬空指针问题
    std::weak_ptr<void> tie_;
    bool tied_ = false;

    /*
    1.由channel的职责：当 epoll_wait 返回某个 fd 的事件时，它只需要执行绑定的逻辑。判断其需要的参数只会为fd、事件，而这些参数在channel类里面都有对应成员，所以用void()就够了
    2.虽然 Channel 回调的签名是 void()，但在实际绑定时，我们可以把任何需要的参数预先“塞”进去
    */
    using ECB = std::function<void()>;
    ECB readcallback_;
    ECB closecallback_;
    ECB errorcallback_;
    ECB writecallback_;

    // 私有方法：向 EventLoop 【请求】同步状态 将 FD 状态同步到内核的实际执行权，从 Channel 身上彻底剥离，交给 EventLoop: “我的状态 (events_) 变了，请我的调度者 (EventLoop) 帮我把这个状态同步到内核去。”
    void update();

    void handleEventwithGuard(); //私有方法，仅内部调用，不提供给外部

public:
    explicit Channel(EventLoop* loop, int fd);
    ~Channel();

    void tie(const std::shared_ptr<void>& obj);

    // 状态查询，供Eventloop使用
    int fd() const;
    uint32_t events() const;
    uint32_t revents() const;
    bool inepoll() const;
    bool iswriting() const;

    void setinepoll(bool on); // 供 EventLoop 更新状态
    // 设置events_
    void useet();  
    void enablereading();
    void disablereading();
    void enablewriting();
    void disablewriting();
    // 设置revents_
    void setrevents(uint32_t ev);


    // 设置readcallback_
    void setreadcallback(ECB fn);
    void setclosecallback(ECB fn);
    void seterrorcallback(ECB fn);
    void setwritecallback(ECB fn);

    
    //-----核心分发器-----
    // 事件分发函数：根据 revents_ 的值，路由到相应的回调函数(如 readcallback_)  ,供EventLoop 调度 Channel 执行业务逻辑
    /*
    Channel 类的核心职责是 事件路由，而不是业务判断;

    职责                                                        谁来承担？
    FD 类型判断：判断是 listenfd 还是 clientfd。                设置回调的阶段。
    事件处理：处理 EPOLLIN、EPOLLOUT 等。                     handleEvent() 方法。

    handleEvent() 不再关心 fd_ 是什么，它只做一件事：将发生的事件 (revents_) 分发给正确的钩子函数。
    */
    void handleEvent(); // 统一提供给外部
};

/*
把 Channel 类的Epoll* ep_改成EventLoop* loop_，本质是让 EventLoop 成为 Channel 与 Epoll 的「唯一中介」—— 彻底解耦 Channel 与底层 IO 复用器（Epoll），强化 EventLoop 的「调度中心」定位，同时为后续扩展（如换 IO 复用器、跨线程任务）铺路。
*/

/*
Channel 类的核心职责是：模式中的核心事件中介者，负责绑定一个文件描述符(FD)，维护其事件状态，并实现事件分发路由。
为了实现其功能，其需要维护的成员变量包括： fd 、 epollfd 、 events 、 revent 、业务逻辑绑定器（包装器）、 状态标记（辅助？？） 
成员函数分为几个层级：核心方法：事件分发与连接  基础方法：状态操作与注册
*/

}