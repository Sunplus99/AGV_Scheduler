#include <myreactor/Connection.h>
#include <cstdio>
#include <cerrno>
#include <unistd.h>
#include <cstring>
#include <myreactor/EventLoop.h>
#include <myreactor/Socket.h>
#include <myreactor/Channel.h>

using namespace myreactor;
/*
**面试官**："你这里的 handle* 函数是什么作用？"

**你（推荐说法）**：
```
"这些 handle* 函数是回调适配器，在 Connection 和 Channel 
两层之间做适配。

比如 handleWrite，当 Channel 通知'fd 可写'时，它负责：
1. 从输出缓冲区发送数据到 socket
2. 更新时间戳、清理缓冲区
3. 如果所有数据发送完，调用上层的 sendCompletecb_ 回调

这种设计的好处是：
- Channel 只关心 epoll 事件分发，不关心业务逻辑
- Connection 处理网络 IO 和缓冲区管理
- TcpServer 只关心应用层的业务逻辑

各层职责清晰，符合单一职责原则。"
*/


// #include "Protocal.h"

/* 针对unique_ptr的参数传递：
场景一：送人（移交所有权）
TcpServer::newconnection :
std::unique_ptr<Socket> sock(new Socket(fd));
Connection* conn = new Connection(loop, std::move(sock), addr);  // // 语义：TcpServer 说 "Socket 我不要了，给你 Connection 吧"
发送方：必须用 std::move()。因为 unique_ptr 是独占的，不能拷贝，只能移动（剪切粘贴）。

接收方必须设计为 std::unique_ptr<Socket>，用来承接这个所有权。如果接收方写成 T*，那只是拿到了地址，并没有拿到“管理权”，原来的 unique_ptr 析构时还是会把对象删掉，导致悬空指针。同时也要用 移动 std::move(clientsock)

场景二：借用（观察者模式） 传递裸指针给unique_ptr管理的对象，仅使用，不能销毁 ，能否修改取决于传的是 T* 还是 const T*
*/

/*shared_ptr的参数传递：
1. 裸指针传递：触发“新建”逻辑 （不成功）
    // C++ 标准库大概长这样：
    template<class Y> 
    explicit shared_ptr(Y* p) { 
        // 1. 申请内存创建 Control Block (控制块)
        cntrl_ = new ControlBlock(); 
        // 2. 引用计数设为 1
        cntrl_->ref_count_ = 1;
        // 3. 保存裸指针
        ptr_ = p;
    }  有explicit的原因：如果没有，用裸指针（包括this指针，T*）传递给一个shared_ptr对象的时候，会触发对应的构造函数，此时会有一个新的控制块（内含计数器），控制同一块内存，会发生double free；所以用explicit禁用掉了，因此不论显示的 = （std::shared_ptr<A> sp = new A();）或者隐式的 = （函数参数传递）代码写上的时候会直接报错，想用也用不了
    explicit 只能防止你无意中把裸指针变成智能指针，还是可以显示调用对应构造函数（不好的行为）
1.5. 双参数别名构造函数:
    std::shared_ptr<T>(ptr, raw_ptr) 是双参数别名构造函数，不会新建控制块，反而会共享原 ptr 的控制块
1.7. static_pointer_cast<T>(context_) 做了两件事：
    // 伪代码：static_pointer_cast 的底层逻辑
    template <typename T, typename U>
    std::shared_ptr<T> static_pointer_cast(const std::shared_ptr<U>& ptr) {
        // 1. 用原生 static_cast 转换裸指针类型(指针指向的对象类型)
        T* raw_ptr = static_cast<T*>(ptr.get());
        // 2. 调用双参数别名构造函数,即创建新的 shared_ptr<T>，共享原 ptr 的引用计数
        return std::shared_ptr<T>(ptr, raw_ptr); 
    }
    编译期完成转换，不做任何运行时类型检查,几乎无性能开销；
2.shared_ptr 传递：触发“拷贝”逻辑 （√）
    // C++ 标准库大概长这样：
    shared_ptr(const shared_ptr& r) {
        // 1. 并不创建新块，而是直接拷贝指针指向同一个块
        cntrl_ = r.cntrl_;
        ptr_ = r.ptr_;
        
        // 2. 对这个已有的块，计数器 +1
        if (cntrl_) {
            cntrl_->ref_count_++;
        }
    }   这正是shared_ptr的正确使用
3.shared_from_this() ： 拷贝逻辑
    shared_ptr<T> shared_from_this() {
    // 它是用 weak_ptr 去构造 shared_ptr
    // ***** 这调用的是 shared_ptr(const weak_ptr& r) 构造函数  *****
    // 这个构造函数的逻辑和上面的“拷贝构造”是一样的：复用控制块，计数 +1
    return shared_ptr<T>(this->weak_this_); 
    }  因此 不能用this，只能用shared_from_this()
4. shared_ptr 参数传递：传值 vs 传引用
    在2.shared_ptr 传递中，提到了拷贝逻辑会让引用计数 +1。这完全正确，但也是性能杀手。
    在函数参数传递中，我们通常有以下原则：
        如果函数内部需要“持有”这个对象（比如存到某个容器里，或者传给别的线程）：
            传值 (shared_ptr<T> ptr)。
            代价： 触发原子操作（Atomic Operation）增加引用计数，有 CPU 开销。
        如果函数内部只是“用一下”（读取数据，计算）：
            传 const 引用 (const shared_ptr<T>& ptr)。
            优势： 不会增加引用计数，就像传递普通指针一样快，但又保证了对象在调用期间不被销毁。
        std::shared_ptr<void> guard = tie_.lock();必须是传值
*/

/*
推荐 auto sock = std::make_unique<Socket>(fd);make_unique
     而不是std::unique_ptr<Socket> sock(new Socket(fd));因为编译器先执行 new T()，然后执行那个会抛异常的函数，最后才构造 unique_ptr。如果中间抛出异常，new 出来的内存就泄露了。

*/

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
    // clientchannel_->enablereading();  
    /*  
        shared_from_this() 放在构造函数外的原因：
            C++ 语法的硬性限制（死穴） 直接原因
        void Connection::connectEstablished() {
            clientchannel_->tie(shared_from_this()); // <--- 重点在这里！！！
            clientchannel_->enablereading();
        }
        shared_from_this() 绝对不能在构造函数中调用！
            原理：shared_from_this() 的原理是去查找管理当前对象的 shared_ptr 控制块。
            矛盾：当 Connection 的构造函数正在执行时，这个对象还是一堆正在初始化的内存，shared_ptr 还没完全生成（因为 new Connection 执行完，才会赋值给 spConnection conn）。
            后果：如果在构造函数里调用 shared_from_this()，程序会直接抛出 std::bad_weak_ptr 异常并崩溃。       
    */
    /*
    “useet() 是属性配置，enablereading() 是启动开关”
    将“对象构造”与“启动关注”分离开来的做法，在软件工程中有一个专门的术语，叫 二段式构造（Two-phase Initialization）。
    多线程并发安全 和 C++ 语法的硬性限制 ，透彻解析为什么必须这么做:
    clientchannel_->enablereading();放在构造函数外的原因：
        clientchannel_->enablereading(); 表示启动监听读事件，并且放在构造函数内的话，【多线程时候，在TcpServer::newconnection内中，会有：先启动监听事件，再绑定对应功能函数，当发生：在绑定之前功能函数之前，就有事件通知，此时channel会调用channel::handleevent，但回调链路并不完整，程序 Core Dump】
        因此clientchannel_->enablereading();必须在所有的绑定之后，即放在构造函数外，由TcpServer::newconnection内手动启动
    clientchannel_->enablereading();放在 clientchannel_->tie(shared_from_this())后的原因：
         调用 enablereading() 时，表示启动监听读事件，此时有事件来的话会调用 channel::handle_event(),而handle_event()会回调Connection的各种应对函数，如果在此之前不完成tie()绑定，那么可能发送Connection对象已销毁，而channel类仍然工作的情况，即非法访问内存（这也是channel --if (tied_) {std::shared_ptr<void> guard = tie_.lock();···）--执行前检查Connection对象是否还活着的原因）
    */
    /*
    综上：clientchannel_->enablereading();需要放在构造函数外，shared_from_this() 也需要放在构造函数外，clientchannel_->enablereading();放在 clientchannel_->tie(shared_from_this())后，因此设计：将两者放在同一个函数内
    */
}

Connection::~Connection() = default;
    // printf("Connection[fd=%d, ip=%s, port=%d] 已销毁\n", fd(), ip().c_str(), port());

int Connection::fd() const {
    // 优先从 Socket 获取，它是资源的持有者
    return clientsock_ ? clientsock_->fd() : -1;
}

// 将其作为 Connection 的值成员存储，可以避免繁琐的 new/delete 和判空逻辑，也更符合“连接一旦建立，对端地址即确定”的物理事实。
std::string Connection::ip() const{
    return clientaddr_.ip();
}

uint16_t Connection::port() const {
    return clientaddr_.port();
}

/*

*/
void Connection::connectEstablished(){
    // 关键一步！将 Channel 与 Connection 的 shared_ptr 进行弱绑定
    // 防止 Connection 析构后 Channel 依然执行回调导致崩溃
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

    /*
    循环内：只负责一件事情——搬运。把内核缓冲区里的数据尽可能快地搬运到用户空间的 inputBuffer_ 中。
    循环外：只负责一件事情——处理。当数据搬运完毕（触发 EAGAIN）或者读取了足够的数据后，再进行协议解析和业务回调。
    */
    // 只要 inputBuffer 不为空，就应该尝试读业务

    // 新逻辑：只要 Buffer 里有数据，就直接把 Buffer 指针扔给上层
    if(!inputBuffer_.isempty()) {
        lastTime_ = Timestamp::now();

        if(readcb_)
            readcb_(shared_from_this(), &inputBuffer_);
    }

    /*
    unpackMessage() -> std::vector<std::string>
        从 Buffer（字节流）中切分出完整的业务包,值返回,这是不可避免的一次拷贝。网络底层是字节流，而业务层需要逻辑对象（string）。这里必须申请内存把字节“变”成对象
    */
    // if(!inputBuffer_.isempty()){
    //     std::vector<std::string> messages = inputBuffer_.unpack();
    //     if(!messages.empty())
    //         lastTime_ = Timestamp::now();
    //     if(readcb_){
    //         for(const auto& msg : messages) 
    //             readcb_(shared_from_this(),msg);
    //     }
    // }
    /*
    handleRead -> readcb_(shared_from_this(), msg)
        参数 1：shared_from_this() ,值传递。延长生命周期。readcb_ 是用户回调，可能会执行很久，或者被投递到工作线程。如果不传 shared_ptr 增加引用计数，万一在回调执行期间对端断开连接（触发 Connection 析构），回调里再操作 Connection 就会 crash。
        参数 2：msg ,msg 已经在 vector 里存在了。传递引用只是传个地址，没有任何内存复制开销。const 保证业务层不会意外修改底层解析好的数据。
    */

    // 2. 处理完数据后，如果刚才标记了错误，再执行错误处理
    if (error) {
        handleError();
    }

}

/*
SIGPIPE是 Unix/Linux 系统定义的标准信号（编号为13），用于通知进程：进程尝试向一个 “已关闭读端的管道” 或 “已关闭连接的套接字” 写入数据。常见触发场景包括：
管道（Pipe）场景：无名管道 / 有名管道的读端被关闭后，写端进程仍向管道写入数据。
套接字（Socket）场景：TCP 连接的对端（如客户端 / 服务端）已关闭连接（调用close()或异常断开），本地进程仍调用send()/write()向该套接字写入数据。
系统对SIGPIPE的默认处理动作是终止当前进程，这是非常 “致命” 的行为 —— 如果未对SIGPIPE做处理，进程会在触发该信号时直接崩溃退出。

MSG_NOSIGNAL
Message No Signal（消息无信号）:当向已关闭的 TCP 套接字发送数据时，不向进程发送SIGPIPE信号，而是让发送函数直接返回错误码。
忽略SIGPIPE是全局生效的（进程内所有写操作都不会因SIGPIPE终止）；
MSG_NOSIGNAL是局部生效的（仅对当前send()调用生效），更灵活。
*/

/*
Connection::send 是服务器发送数据的统一入口。
    首先针对跨线程的情况：
        当调用者（Worker 线程）调用 send 时，后续的 IO 操作是异步在 IO 线程执行的。这带来了两个致命的生命周期竞态问题：
            1.连接对象的生命周期：异步执行时，Connection 可能已被销毁。我利用 Lambda 的值捕获机制配合 shared_from_this()，增加了引用计数，为 Connection “续命”，确保其活到任务执行结束。
            2.数据的生命周期：待发送数据通常在 Worker 线程的栈上。为了防止异步执行时栈内存失效导致野指针，我必须进行深拷贝。
        在数据容器的选择上，我坚决放弃了 char* 手动管理内存的方案，而是选择了 std::string，原因有二：
            ①安全性（RAII）：利用 C++ 的 RAII 机制，Lambda 销毁时 String 自动析构，彻底根除内存泄漏风险。
            ②性能（SSO 优化）：这是关键点。现代 std::string 具备 小字符串优化（SSO）。对于高频的小包（如心跳、ACK），数据直接存放在对象内部的栈空间，避免了昂贵的堆内存分配（malloc），缓存命中率极高。既有栈的高效，又有堆的灵活。
        其次，针对 IO 线程场景（本线程调用）： 虽然没有竞态风险，但我依然统一构造了 std::string，主要是为了：
            1.统一接口，使得两种情况复用一份代码
            2.当需要将发送数据加入outputbuffer时，我们可以直接将临时的string对象move 进缓冲区，避免二次拷贝，并不会造成很大的额外开销。
*/

/*
Connection::send 是服务器发送数据的统一入口。设计上重点解决了两个维度的难题：

一、跨线程调用（Worker 线程 -> IO 线程）：
    此时后续的 IO 操作是异步执行的，面临严峻的“生命周期竞态”：
    1. 连接对象的生命周期：Connection 可能在任务执行前被销毁。
       -> 解决方案：利用 Lambda 的【值捕获】配合 shared_from_this()，增加引用计数，为 Connection 强行“续命”。
    2. 数据的生命周期：待发送数据在 Worker 线程的栈上，异步执行时栈可能已失效。
       -> 解决方案：必须进行【深拷贝】。我选择了 std::string 而非 char*，理由如下：
          ① 安全性 (RAII)：Lambda 销毁时 string 自动析构，彻底杜绝内存泄漏。
          ② 性能 (SSO)：这是关键。现代 std::string 对短包（如心跳、ACK）有 SSO 优化，直接在栈上分配，避免了昂贵的堆内存分配 (malloc)，既有栈的高效又有堆的灵活。

二、本线程调用（IO 线程 -> IO 线程）：
    这是 AgvServer 发送业务包的主战场。
    -> 解决方案：采用【透传策略】。
    我剥离出了 sendInLoopCore 核心逻辑。在本线程内，直接透传 Buffer 的原始指针 (char* + len)，完全跳过了 std::string 的构造。
    这意味着在最频繁的正常发送路径上，我们实现了【零内存分配 (Zero Allocation)】和【零拷贝传递】，达到了性能的理论上限。
*/

// 1. 发送 Raw Data (const char*)
void Connection::send(const char* data, size_t size){
    if(fd() == -1) return;

    /*
    Connection::send 里的显式判断，是为了在最常见路径（Happy Path）上省去创建回调对象的开销。这是高性能库的典型特征——“能省一点是一点”。
    */
    // 判断当前线程
    if(loop_->isInLoopThread()){
        sendInLoopCore(data, size);
    } else{
        // 跨线程调用
        /*
        loop_->runInLoop(
            std::bind(&Connection::sendInLoop,
                      shared_from_this(), // 1.值传递。延长生命周期.计数+1
                      std::string(data, size) // 2.构造临时 string 对象，拷贝数据
            )
        );
        // 现代 C++ 写法
        C++14 引入的一个超级强大的特性，学名叫 “广义捕获” (Generalized Lambda Capture) 或 “初始化捕获” (Init Capture)
        从 C++14 开始，我们可以在捕获列表里无中生有地定义新变量并初始化它
        */

        /*跨线程委托
        IO 线程大哥，我（Worker）算好了一些数据要发给这个客户端。我知道我不能直接动 Socket，所以我把数据和发送指令打包发到你的邮箱（任务队列）。等你忙完手头的 epoll_wait，麻烦帮我发一下
        */
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
                // EventLoop::queueInLoop 的设计意图、调用目的与核心价值
                /*我们将 queueInLoop 暴露为 public 接口，并在 sendInLoop 中显式调用它，本质上是实现了一种 “应用层的主动让权（Yield）” 机制。
                【核心目的：防止回调抢占，保障 IO 调度的公平性】
                我们将 sendCompletecb_ 的执行时机，人为地从当前的执行流中剥离，强制推迟到当前 Loop 的任务队列末尾（或下一轮 Loop）。这带来了三大架构收益：
                1.保护程序，即切断了潜在的无限递归调用链。
                2.消除队头阻塞
                    无工作线程模式下，sendInLoop 通常发生在 handleEvent（IO 事件处理）阶段。如果不入队直接执行，耗时的回调逻辑会“插队”在其他活跃连接的 IO 事件之前。通过 queueInLoop，我们将回调迁移到 doPendingFunctors 阶段，确保了当前轮次中所有连接的 read/write 事件都能被优先、快速地处理。
                3.用统一的任务调度模型来保证公平
                    对于无工作线程模式：保障了新产生的非 IO 任务不干扰正常的 IO 处理；
                    对于工作线程模式：虽然 sendInLoop 本身已在 doPendingFunctors 阶段，即便如此，但显式调用 queueInLoop 依然有意义，它将新产生的回调任务再次放入队列尾部（触发 wakeup 留给下一轮处理），避免了“任务生任务”并且抢占执行的现象。
                */
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
    /*
    如果是Worker调用了handleClose(),
    最后持有的shared_ptr的线程是Worker线程，并且此线程后续会调用~channel，内部会调用： IO_EventLoop->removeChannel(this)，也就是说：Worker线程 修改了 IO 线程正在遍历或使用的 EventLoop 内部数据结构 IO_epoll，此时会出现 Worker 线程 和 IO 线程同时访问 一处资源的 情况，引发冲突 ; 而我们通过 One Loop Per Thread 模型 来实现 无锁 【无锁编程】 的临界资源管理,即 只有在 IO线程中才能真正执行关闭动作 , 为的是 线程安全
    */
    // 2. 只有在 IO 线程中才能真正执行关闭动作（线程安全）
    if (loop_->isInLoopThread()) {
        handleClose(); 
    } else {
        // 如果是 Worker 线程调用的，切回 IO 线程去关
        loop_->queueInLoop(std::bind(&Connection::forceClose, shared_from_this()));
    }
}