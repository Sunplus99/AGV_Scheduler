#include <myreactor/TcpServer.h>
#include <myreactor/Acceptor.h>
#include <myreactor/Connection.h>
#include <myreactor/InetAddress.h>
#include <myreactor/EventLoopThreadPool.h>
#include <myreactor/Socket.h>
#include <myreactor/Buffer.h>

using namespace myreactor;

/*
要摒弃的是：“主线程分配对象内存 -> 扔进线程池运行”。 要建立的是：“专用线程对象管理 -> 线程启动 -> 栈上创建 EventLoop -> 暴露接口”
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

    ioLoopPool_ = std::make_unique<EventLoopThreadPool>(&mainloop_, threadnum_);
    
}




TcpServer::~TcpServer(){ // C++ 中类的成员按声明顺序初始化，按逆序析构
    stop(); // 兜底，防止用户忘了调 EchoServer::Stop
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
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conns_[connfd] = conn;  // 加入map
    }
    // 6.先创建绑定再回调：保证回调触发时，对象已具备完整能力
    ioloop->runInLoop(std::bind(&Connection::connectEstablished, conn));

    // 7.回调 计数：3 -> 4
    if(newconncb_) newconncb_(conn);
    
} 

void TcpServer::onTimer() {
    // 定时任务编排
    if(tickcb_) tickcb_();
    
    // 空闲清理
    cleanIdleConnections();
}

void TcpServer::cleanIdleConnections() {
    if(timeoutSec_ <= 0) return;

    Timestamp now = Timestamp::now();

    std::vector<spConnection> timeOutConns; // 临时存放待清理的连接

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

    }

} //  



// 统一的清理函数
void TcpServer::removeconnection(const spConnection& conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 这里的 conn 参数持有引用计数，保证了在 conn 在作用域结束之前不会死
    if (conns_.find(conn->fd()) != conns_.end()) {
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
    removeconnection(conn);
}


void TcpServer::readconnection(const spConnection& conn, Buffer* buf){
    // printf("TcpServer: 收到客户端[fd=%d]消息：%s\n",connfd,message.c_str());
    if(onmesscb_) onmesscb_(conn, buf);
}

void TcpServer::sendcomplete(const spConnection& conn){
    // printf("TcpServer: 客户端[fd=%d]数据发送完成\n", connfd);
    if(sendcompcb_) sendcompcb_(conn);
}



void TcpServer::setnewconncb(const CB& fn) {newconncb_ = fn;}
   
void TcpServer::setclosecb(const CB& fn) {closecb_ = fn;}
    
void TcpServer::seterrorcb(const CB& fn) {errorcb_ = fn;}

void TcpServer::setsendcompcb(const CB& fn) {sendcompcb_ = fn;}
    
void TcpServer::setonmesscb(const MCB& fn) {onmesscb_ = fn;}

void TcpServer::setconntimeoutcb(const CB& fn) {conntimeoutcb_ = fn;}