#pragma once

namespace myreactor{

class InetAddress;

/*
Socket 类的核心职责是：
拥有并管理一个操作系统文件描述符 (fd_)。
封装所有与该 fd 相关的系统调用（bind, listen, accept, setsockopt 等）。
遵循 RAII 原则（Resource Acquisition Is Initialization），在析构时自动关闭 fd_。*/

/*
我想的构造函数包括1.socket()系统调用返回的listenfd 2.accept4返回的clientfd 这两种输入，并且没有想到设置一个独立的int createnonblocking();函数

思考后明白这其实输入形参都是int fd，但是我觉得还需要一个socket()成员方法来创建listenfd的成员变量，用成员变量两创建一个对象，而按照我的想法socket()成员方法需要依赖于对象才能调用，而对象又需要socket()成员方法来创建，有点循环依赖的感觉，但是单独设置一个int createnonblocking();感觉很奇怪，所以有点疑惑，你为我从类的设计角度再分析一下，清晰的逻辑应是怎样的？这里设置一个静态成员函数是不是更好？

"如果让构造函数负责创建 fd，那就需要两个不同的构造函数签名，或者使用一个不明确的布尔参数，这破坏了清晰性。
解决这个矛盾的最佳实践是使用 *静态工厂方法 (Static Factory Method)*。
静态方法 (Static Member Function) 不依赖于任何对象实例，它可以完成对象的前置工作（比如创建资源），然后将准备好的资源传递给构造函数。"

“独立设置一个 int createnonblocking() 感觉很奇怪”：
回答： 独立函数是 C 语言的习惯，在 C++ 中，静态成员函数是更好的选择，因为它将创建逻辑和 Socket 类在命名空间上关联起来，更加面向对象。
这种设计将 “资源的创建” (CreateNonblocking) 和 “资源的管理” (Socket(int fd)) 分离，是 C++ 资源管理中的一个经典且优雅的设计模式。
*/

class Socket{
private:
    // socketfd
    const int fd_;
public:
    // 构造与析构函数 1.socket()系统调用返回的listenfd 2.accept4返回的clientfd
    static int createNonblocking();

    explicit Socket(int fd);

    ~Socket();

    // 核心方法 1.返回fd 2.设置方法 3.网络操作方法（bind,listen,accept)
    int fd() const;

    void setreuseaddr(bool on);

    void settcpnodelay(bool on);

    void bind(const InetAddress& servaddr);

    void listen(int nn=128);

    int accept(InetAddress& clientaddr);

    
};

}