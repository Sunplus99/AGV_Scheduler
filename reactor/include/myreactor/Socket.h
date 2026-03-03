#pragma once

namespace myreactor{

class InetAddress;


class Socket{
private:
    // socketfd
    const int fd_;
public:
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