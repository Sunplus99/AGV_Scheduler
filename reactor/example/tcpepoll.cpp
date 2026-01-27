#include <stdio.h>
#include <signal.h>
#include "EchoServer.h"

// 全局指针，用于在信号处理函数中访问 EchoServer 实例
EchoServer* g_server = nullptr;

// 信号处理函数
void stop_handler(int sig){
    printf("\n捕获到信号 %d,正在停止服务器...\n",sig);
    if(g_server){
        g_server->Stop();
    }
}

int main(int argc,char* argv[]){
    
    if(argc != 3){
        printf("usage:./tcpepoll ip port\n");
        printf("example:./tcpepoll 192.168.184.128 5005\n");
        return -1;
    }

    // 注册信号捕获
    signal(SIGINT, stop_handler);   // SIGINT 对应 Ctrl+C
    signal(SIGTERM, stop_handler);  // SIGTERM 对应 kill 命令

    EchoServer echoserver(argv[1], atoi(argv[2]));

    g_server = &echoserver;

    printf("Server started. Press Ctrl+C to stop.\n");
    echoserver.Start();

    printf("Server stopping... cleaning up resources.\n");
    return 0;
}