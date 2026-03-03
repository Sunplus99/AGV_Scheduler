#include <myreactor/Socket.h>
#include <myreactor/InetAddress.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <errno.h>
#include <cstring>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

using namespace myreactor;

int Socket::createNonblocking(){
    int listenfd = socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK,0);
    if(listenfd < 0){
        printf("%s:%s:%d listen socket create error:%d\n",__FILE__,__FUNCTION__,__LINE__,errno);
        exit(-1);
    }
    return listenfd;
}

Socket::Socket(int fd):fd_(fd){}

Socket::~Socket() {::close(fd_);}

int Socket::fd() const {return fd_;}

void Socket::setreuseaddr(bool on){
    int optval = on ? 1 : 0;
    ::setsockopt(fd_,SOL_SOCKET,SO_REUSEADDR,&optval,sizeof(optval));
}

void Socket::settcpnodelay(bool on){
    int optval = on ? 1 : 0;
    ::setsockopt(fd_,IPPROTO_TCP,TCP_NODELAY,&optval,sizeof(optval));
}

void Socket::bind(const InetAddress& servaddr){
    if(::bind(fd_,servaddr.getSockAddr(),servaddr.len()) < 0){
        perror("bind() failed");
        close(fd_);
        exit(-1);
    }
}

void Socket::listen(int nn){
    if(::listen(fd_,nn) < 0){
        perror("listen() failed");
        close(fd_);
        exit(-1);
    }
}

int Socket::accept(InetAddress& clientaddr){
    struct sockaddr_in peeraddr;
    socklen_t len = sizeof(peeraddr);
    int clientfd = accept4(fd_,(struct sockaddr*)&peeraddr,&len,SOCK_NONBLOCK);

    clientaddr.setaddr(peeraddr);

    return clientfd;
}