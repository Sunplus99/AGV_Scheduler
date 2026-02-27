#include "EchoServer.h"
#include <myreactor/EventLoop.h>
#include <myreactor/Connection.h>
#include <myreactor/Buffer.h>  
#include <iostream>
#include <unistd.h>
#include <sys/syscall.h>   
#include <cstring>
/*
class EventLoop;

class EchoServer{
private:
    TcpServer tcpserver_;

public:
    EchoServer(const std::string& ip, const uint16_t port);
    ~EchoServer();

    void Start();

    void HandleNewConn(int connfd);
    void HandleClose(int connfd);
    void HandleError(int connfd);
    void HandleMessage(int connfd, std::string message);
    void HandleSeCom(int connfd);
    void HandleTimeOut(EventLoop *loop);
};
*/

/*3 种消除警告的方法
    方案 A：C++17 [[maybe_unused]]（最推荐，因为你用了 C++17）
        这是现代 C++ 最优雅、语义最清晰的写法。它直接告诉编译器：这个变量可能不被使用，别报警。
        void EchoServer::HandleNewConn([[maybe_unused]] const spConnection& conn){}
    方案 B：省略参数名（最常用，兼容性最好）
        在 C++ 中，如果一个参数在函数体内不使用，你可以只写类型，不写变量名（或者把变量名注释掉）。这样编译器就不会把它当作“未使用的变量”了。
        void EchoServer::HandleNewConn(const spConnection&) {}
    方案 C：(void) 强转（老派写法）
        这是 C 语言遗留下来的习惯，在老代码库很常见，也能解决问题
        void EchoServer::HandleNewConn(const spConnection& conn) {(void)conn; // 假装用了一下，欺骗编译器}
*/

EchoServer::EchoServer(const std::string& ip, const uint16_t port, int iothreadnum, int workthreadnum)
    :tcpserver_(ip,port,iothreadnum),
     threadpool_(workthreadnum, "WORKER")
{
    tcpserver_.setnewconncb(
        std::bind(&EchoServer::HandleNewConn, this, std::placeholders::_1)
    );
    tcpserver_.setclosecb(
        std::bind(&EchoServer::HandleClose, this, std::placeholders::_1)
    );
    tcpserver_.seterrorcb(
        std::bind(&EchoServer::HandleError, this, std::placeholders::_1 )
    );
    tcpserver_.setsendcompcb(
        std::bind(&EchoServer::HandleSeCom, this, std::placeholders::_1)
    );
    tcpserver_.setonmesscb(
        std::bind(&EchoServer::HandleMessage, this, std::placeholders::_1, std::placeholders::_2 )
    );
    tcpserver_.setconntimeoutcb(
        std::bind(&EchoServer::HandleConnTimeOut, this, std::placeholders::_1)
    );

    tcpserver_.setTimeOut(20);

}

EchoServer::~EchoServer() {}

void EchoServer::Start(){
    tcpserver_.start();
}

/*
幂等性检查：负责协调两者，避免重复操作。
*/
void EchoServer::Stop(){
    threadpool_.stop();
    tcpserver_.stop();
}

void EchoServer::HandleNewConn(const spConnection& /*conn*/){
    // std::cout << "New Connection Come in [fd=" << conn->fd() << "ip: " << conn->ip() << ":" << conn->port()<< "]" <<std::endl;

    // 根据业务的需求，在这里可以增加其它的代码。
}

void EchoServer::HandleClose(const spConnection& conn){
    std::cout << "EchoServer conn closed. FD:" << conn->fd() << std::endl;

    // 根据业务的需求，在这里可以增加其它的代码。
}

void EchoServer::HandleError(const spConnection& conn){
    std::cout << "EchoServer conn error. FD:" << conn->fd() << std::endl;

    // 根据业务的需求，在这里可以增加其它的代码
}

// void EchoServer::HandleMessage(const spConnection& conn, const std::string& message){

//     std::string replyMsg = "reply: " + message;
    
//     // 调用TcpServer的发送接口 （IO层）
//     // bool sendOk = tcpserver_.sendToClient(connfd, replyMsg.data(), replyMsg.size());
//     // if(!sendOk) std::cout << "EchoServer: 发送数据到fd="<< connfd << "失败" << std::endl;
//     conn->send(replyMsg.data(), replyMsg.size());
// }

// std::string EchoServer::onMessage(const std::string& message) {
//     return ""; // 暂时没用到
// }


/*
EchoServer -> conn->send(const char* data, size_t len),传递方式：指针 + 长度
    保证接口通用性，send 接口不应该强迫用户必须传 std::string。用户的数据可能在 vector<char> 里，也可能在栈数组 char buf[] 里。接受指针是最灵活的（View 语义）。
*/
void EchoServer::HandleMessage(const spConnection& conn, myreactor::Buffer* buf){

    // 协议处理：循环解包，处理粘包情况
    // 协议格式：[4字节长度] + [消息体]
    while (buf->size() >= 4) {
        // 1. 读取消息长度（不移动读指针）
        int32_t msglen = buf->peekInt32();

        // 2. 检查是否收到完整消息（处理分包）
        if (buf->size() < static_cast<size_t>(4 + msglen)) {
            break;  // 数据不完整，等待下次接收
        }

        // 3. 跳过长度头
        buf->erase(4);

        // 4. 读取消息体
        std::string message = buf->readAsString(msglen);

        // 5. 业务逻辑：回显
        auto bussinessLogic = [conn, message, msglen]{
            // 重新打包：[4字节长度] + [消息体]
            std::string response;
            response.resize(4 + msglen);
            memcpy(&response[0], &msglen, 4);              // 写入长度头
            memcpy(&response[4], message.data(), msglen);  // 写入消息体

            // 发送数据
            // 如果在 IO 线程，sendInLoop 会被直接调用
            // 如果在 Worker 线程，会通过 runInLoop 转发回 IO 线程
            conn->send(response.data(), response.size());
        };

        // 逻辑分流
        if(threadpool_.size() > 0) threadpool_.addtask(std::move(bussinessLogic));
        else
            bussinessLogic();
    }
}


// 这是现代 C++ 最优雅、语义最清晰的写法。它直接告诉编译器：这个变量可能不被使用，别报警。
void EchoServer::HandleSeCom([[maybe_unused]]const spConnection& conn){
    // std::cout << "Message send complete." << std::endl;

    // 根据业务的需求，在这里可以增加其它的代码。
}

void EchoServer::HandleConnTimeOut(const spConnection& conn){
    std::cout << "Connection "<< conn->fd()<<" timeout." << std::endl;
    // 根据业务的需求，在这里可以增加其它的代码。
}