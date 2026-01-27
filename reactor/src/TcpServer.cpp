#include <myreactor/TcpServer.h>
#include <myreactor/Acceptor.h>
#include <myreactor/Connection.h>
#include <myreactor/InetAddress.h>
#include <myreactor/EventLoopThreadPool.h>
#include <myreactor/Socket.h>
#include <myreactor/Buffer.h>

using namespace myreactor;

/*
我们要摒弃的是：“主线程分配对象内存 -> 扔进线程池运行”。 我们要建立的是：“专用线程对象管理 -> 线程启动 -> 栈上创建 EventLoop -> 暴露接口”
*/

TcpServer::TcpServer(const std::string &ip, const uint16_t& port, int threadnum):threadnum_(threadnum),port_(port){  // C++ 中类的成员按声明顺序初始化，按逆序析构
    mainloop_.settimercb(
        std::bind(&TcpServer::onTimer,this)
    );  // 绑定定时回调
 
    InetAddress servaddr(ip,port);
    acceptor_ = std::make_unique<Acceptor>(&mainloop_,servaddr);
    acceptor_->setnewconnectioncb(
        std::bind(&TcpServer::newconnection,this,std::placeholders::_1,
        std::placeholders::_2));

    // 3. --- 核心修改：初始化 IO 线程池 ---
    // 这里不再手动循环创建 Loop，而是把任务交给 EventLoopThreadPool
    // 注意：此时线程还没启动，start() 的时候才启动
    ioLoopPool_ = std::make_unique<EventLoopThreadPool>(&mainloop_, threadnum_);
    
    
    // for(int ii=0 ; ii<threadnum_; ++ii){
    //     // subloops_.emplace_back(new EventLoop); //这里用emplace_back，直接用new返回的裸指针构造出vector的合法成员（unique_ptr）并加入然而，如果emplace_back执行过程中发生异常（比如vector扩容时内存分配失败），new EventLoop已经分配的EventLoop对象内存会无法被释放，导致资源泄漏：
    //     subloops_.emplace_back(std::make_unique<EventLoop>()); //此时std::make_unique会先构造unique_ptr，再将其移动到 vector 中。即使emplace_back抛出异常，unique_ptr的析构函数也会自动释放已分配的EventLoop对象；
    //     subloops_[ii]->settimeoutcb(
    //     std::bind(&TcpServer::epolltimeout,this,std::placeholders::_1));
    //     threadpool_->addtask(std::bind(&EventLoop::run, subloops_[ii].get()));
    // }
    
}


/*
    【优雅退出的核心流程】
    
    1. 触发时机：
       主线程想要退出（或 TcpServer 析构）时，触发 RAII 链式析构：
       ~EventLoopThreadPool -> 销毁 threads_ 容器 -> 触发 ~EventLoopThread。

    2. 核心风险（如果不做处理）：
       如果主线程析构了 EventLoopThread 对象（包括其中的 mutex_, loop_ 指针等），而后台 IO 线程还在运行：
       - 风险 A (Use-After-Free)：IO 线程可能会访问已经被释放的内存成员。
       - 风险 B (Crash)：C++ 标准规定，如果 std::thread 析构时仍处于 joinable 状态，程序将直接调用 std::terminate() 导致进程崩溃。

    3. 解决方案（Join）：
       我们在 ~EventLoopThread 中使用 if(thread_.joinable()) thread_.join(); 
       这会强制主线程阻塞等待，直到 IO 线程完全结束运行。这保证了“先停止线程，后销毁对象”的时序安全。

    4. 前置条件（Stop）：
       为了让 join() 能成功返回（避免死锁），必须先让 IO 线程跳出循环。
       这需要两步操作，封装在 EventLoop::stop() 中：
       ① 设置 quit_ = true（打破 while 循环条件）。
       ② 调用 wakeup()（打破 epoll_wait 阻塞，防止 IO 线程一直在睡大觉）。
       
    总结：先 stop() 唤醒并通知退出，再 join() 等待回收，最后安全析构。
*/


TcpServer::~TcpServer(){ // C++ 中类的成员按声明顺序初始化，按逆序析构
    stop(); // 兜底，防止用户忘了调 EchoServer::Stop
    // delete acceptor_;
    

    // for(auto& pair : conns_) delete pair.second;
    // for(auto& loop : subloops_) delete loop;
    // delete threadpool_;

    // conns_.clear();
}

void TcpServer::start(){
    ioLoopPool_->start();
    acceptor_->listen();
    mainloop_.run();
}

void TcpServer::stop() {
    mainloop_.stop();
}

void TcpServer::setTimeOut(int timeout) {
    timeoutSec_ = timeout;
} 

// Connection* TcpServer::getConnection(int connfd) const {
//     auto it = conns_.find(connfd);
//     return (it!=conns_.end()) ? it->second : nullptr;
// }

// bool TcpServer::sendToClient(int connfd, const char* data, size_t size){
//     auto it = conns_.find(connfd);
//     if (it != conns_.end() && data != nullptr && size > 0) {
//         it->second->send(data, size);
//         return true;
//     }
//     return false; 
// }

void TcpServer::newconnection(int connfd, const InetAddress& clientaddr){
    // --- 核心修改：从池中获取 Loop ---
    // 这里的 getNextLoop() 内部使用轮询算法 , 无需绑定定时回调
    EventLoop* ioloop = ioLoopPool_->getNextLoop();

    //2.创建Socket
    auto clientsock = std::make_unique<Socket>(connfd);
    
    //3.创建Connection  计数：1 ; 持有者：栈上的局部变量 conn
    auto conn = std::make_shared<Connection>(ioloop, std::move(clientsock), clientaddr);
    
    //4.设置回调
    conn->setclosecb(
        std::bind(&TcpServer::closeconnection,this,std::placeholders::_1)
    );
    conn->seterrorcb(
        std::bind(&TcpServer::errorconnection,this,std::placeholders::_1)
    );
    conn->setreadcb(
        std::bind(&TcpServer::readconnection,this,std::placeholders::_1,std::placeholders::_2)
    );
    conn->setsendCompletecb(
        std::bind(&TcpServer::sendcomplete,this,std::placeholders::_1)
    );

    //5.加入容器 计数：1 -> 2 ; 持有者：栈上的局部变量 conn + TcpServer 的成员变量 conns_ (Map)。
    /*
    加入容器前：如果不存入 conns_，一旦出了 newConnection 函数的作用域，shared_ptr 计数归零，刚建立的连接就会立刻断开（析构）。
    加入容器后：TcpServer 成为了这个连接的“长期监护人”。
    */
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conns_[connfd] = conn;  // 加入map
    }
    // 6.先创建绑定再回调：保证回调触发时，对象已具备完整能力
    // conn->connectEstablished();
    
    /*
    现状：conn 对象刚刚在主线程（Main Reactor）的 TcpServer::newConnection 中被 new 出来。此时，它只是堆上的一块内存，还没有真正“活”过来（没有注册到 epoll）。
    矛盾：主线程负责 Accept，但这个新连接后续的读写归子线程（Sub Reactor / ioloop）管。
    动作：connectEstablished 的核心工作是 channel_->enableReading()（即调用 epoll_ctl 注册事件）。
        “子线程啊，这是我刚接收的一个连接，现在我把它过继给你了。请你在你的线程里执行 connectEstablished，把它注册到你的 epoll 实例里。从此以后，它归你管，我不再插手它的 IO 操作。”
    */
    
    /*这里为什么是 conn (spConnection)？
    1. 语法的必然性
    Connection::connectEstablished 是一个成员函数。在 C++ 中，要回调成员函数，必须提供一个对象实例（也就是 this 指针）。std::bind 的第一个参数是函数地址，第二个参数就是这个对象实例
    2. 生命周期的“续命丸” (核心原因),这是一个典型的跨线程异步调用场景：
    IO 线程也拿到了一个sp，保证任务执行时对象活着。
    动作执行时：ioloop（IO 线程）可能在几毫秒后才会执行这个 connectEstablished 任务。
    */ 
    // 计数：2 -> 3 ; 持有者：栈变量 + conns_ Map + IO 线程的任务队列（Bind 对象内部）。
    ioloop->runInLoop(std::bind(&Connection::connectEstablished, conn));

    /*
    第6步是异步执行的，且这些回调函数的赋值在创建对象的时候就全部建立完成了，并不依赖connectEstablished ， 即 异步并发：在两条不同的时间线上
    */
    // 7.回调 计数：3 -> 4
    if(newconncb_) newconncb_(conn);
    
} // 计数：4 -> 2 栈上的局部变量 conn 销毁了；newconncb_(conn)里的也销毁了
  // IO 线程执行完 connectEstablished，任务对象销毁。 ; 计数：2 -> 1 ; 持有者：仅剩 TcpServer 的 conns_ Map

void TcpServer::onTimer() {
    // 定时任务编排
    if(tickcb_) tickcb_();
    
    // 空闲清理
    cleanIdleConnections();
}

void TcpServer::cleanIdleConnections() {
    if(timeoutSec_ <= 0) return;

    /* now 放在锁外:
    1.缩短临界区时间：可以减少系统调用开销，即放在锁外：只调用 1 次系统调用。放在锁内：如果有 10,000 个连接，则需要在锁里调用 10,000 次系统调用，IO 线程如果这时候想连进来，就会被阻塞很久，导致并发性能下降
    3.在进入循环前拍了一张快照（Snapshot），所有连接都基于这一瞬间的时间点来决定去留，逻辑更清晰、确定
    */
    Timestamp now = Timestamp::now();
    /*
    引入 timeOutConns（这种技巧常被称为“延长生命周期”或“延迟销毁”）的核心目的不是为了内存管理，而是为了把“对象的析构过程”移出“锁的保护范围”,缩短“持有锁的时间”
    */
    std::vector<spConnection> timeOutConns; // 临时存放待清理的连接

    /*{
        // 锁的范围仅限于遍历和从 map 中移除，不包含 Connection 的析构
        std::lock_guard<std::mutex> lock_(mutex_);
        for (auto it = conns_.begin(); it != conns_.end(); ++it){
            if(it->second->istimeout(now,timeoutSec_)){
                timeOutConns.push_back(it->second);
                conns_.erase(it);
                
运行时致命错误 ——erase(it)后迭代器失效，++it会导致程序崩溃。这是最核心的致命错误，也是容器遍历删除的经典坑：
    当执行conns_.erase(it);后，迭代器it会立即失效（变成「野迭代器」），它指向的内存已经被容器释放；下一轮循环会执行++it，对失效的野迭代器进行自增操作，会直接触发段错误（SIGSEGV），导致程序崩溃；即使没崩溃，也会导致部分连接漏检查，无法完整遍历conns_。
正确解决方案：利用erase的返回值 ——std::map/std::unordered_map的erase(it)会返回【被删除元素的下一个有效迭代器】，用这个返回值更新it，避免失效。
                
            }
        }
    }*/
    {
        // 锁的范围仅限于遍历和从 map 中移除，不包含 Connection 的析构
        std::lock_guard<std::mutex> lock_(mutex_);
        for (auto it = conns_.begin(); it != conns_.end(); /*++it*/){
            if(it->second->istimeout(now,timeoutSec_)){
                timeOutConns.push_back(it->second);
                it = conns_.erase(it); //此时已经指向下一个了，无需在++it，因此需要手动设置++it
            }else  
                ++it;
        }
    }

    // 【第二步：在锁外处理业务逻辑】
    for (const auto& conn : timeOutConns) {
        // 1. 如果你设置了专门的超时回调，在这里调
        if (conntimeoutcb_) {
            conntimeoutcb_(conn); // 告诉用户：是这个家伙超时了
        }

        // 2. 即使不调专门的回调，conn 析构时也会自动触发 CloseCallback
        //    或者你可以显式调用: conn->connectDestroyed(); 
    }

} //  // 当 timedOutConns 析构时，引用计数归零，触发 ~Connection ,在这里慢慢析构，反正不占锁




// 统一的清理函数
void TcpServer::removeconnection(const spConnection& conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 这里的 conn 参数持有引用计数，保证了在 conn 在作用域结束之前不会死
    if (conns_.find(conn->fd()) != conns_.end()) {
        // delete it->second;   // 触发 Connection 析构 -> Channel 析构 -> Epoll 移除
        //erase()方法填入参数可以是key、迭代器、按迭代器范围删除（iterator erase(iterator first, iterator last)）
        // conns_.erase();    // 从容器移除
        conns_.erase(conn->fd());
    } else {
        printf("TcpServer: 客户端[fd=%d]未找到，无需清理\n", conn->fd());
}
}

void TcpServer::closeconnection(const spConnection& conn) {
    // 二、先回调后清理：保证业务收尾时，底层资源仍可用
    if(closecb_) closecb_(conn);
    // printf("TcpServer: 客户端[fd=%d]正常关闭，清理资源\n", connfd);
    removeconnection(conn);
}

void TcpServer::errorconnection(const spConnection& conn) {
    if(errorcb_) errorcb_(conn);
    // printf("TcpServer: 客户端[fd=%d]异常错误，清理资源并告警\n", connfd);
     // 扩展：可添加告警日志、监控上报、错误统计等逻辑
    removeconnection(conn);
}

/*
TcpServer::readconnection -> EchoServer::HandleMessage
    参数 1：const spConnection& conn，Const 引用传递 shared_ptr
        性能优化。
            上一层已经持有了 shared_ptr（保证对象活着）。
            shared_ptr 的拷贝包含原子操作（引用计数 +1/-1），这是有 CPU 开销的。
            既然这一层只是“路过”或者是同步调用，用 const& 既能访问对象，又省去了原子操作的开销。
    参数 2：const std::string& message Const 引用传递。
            继续零拷贝接力。只要还在【同一个线程栈内调用，数据就不用动】
*/
void TcpServer::readconnection(const spConnection& conn, Buffer* buf){
    // printf("TcpServer: 收到客户端[fd=%d]消息：%s\n",connfd,message.c_str());
    if(onmesscb_) onmesscb_(conn, buf);
}

void TcpServer::sendcomplete(const spConnection& conn){
    // printf("TcpServer: 客户端[fd=%d]数据发送完成\n", connfd);
    // 扩展：可添加业务逻辑，比如记录发送成功日志、触发下一次数据发送等
    if(sendcompcb_) sendcompcb_(conn);
}

// void TcpServer::epolltimeout(EventLoop* loop){
//     // printf("TcpServer: epolltimeout\n");
//     if(timeoutcb_) timeoutcb_(loop);
// }

void TcpServer::setnewconncb(const CB& fn) {newconncb_ = fn;}
   
void TcpServer::setclosecb(const CB& fn) {closecb_ = fn;}
    
void TcpServer::seterrorcb(const CB& fn) {errorcb_ = fn;}

void TcpServer::setsendcompcb(const CB& fn) {sendcompcb_ = fn;}
    
void TcpServer::setonmesscb(const MCB& fn) {onmesscb_ = fn;}

void TcpServer::setconntimeoutcb(const CB& fn) {conntimeoutcb_ = fn;}