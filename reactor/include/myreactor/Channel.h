#pragma once
#include <functional>
#include <memory>

namespace myreactor {

class EventLoop;

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

    using ECB = std::function<void()>;
    ECB readcallback_;
    ECB closecallback_;
    ECB errorcallback_;
    ECB writecallback_;

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
    void handleEvent(); // 统一提供给外部
};


}