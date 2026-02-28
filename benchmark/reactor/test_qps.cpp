#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <iostream>

// 全局原子计数器，记录处理成功的请求总数
std::atomic<long> g_request_count(0);
// 控制测试运行的开关
volatile bool g_running = true;

// 封装一个保证读满 N 字节的函数
ssize_t readn(int fd, void *vptr, size_t n) {
    size_t  nleft;
    ssize_t nread;
    char   *ptr;

    ptr = (char*)vptr;  // 转换为char*，方便字节级操作
    nleft = n;
    while (nleft > 0) {
        if ( (nread = recv(fd, ptr, nleft, 0)) < 0) {
            if (errno == EINTR)
                nread = 0;      /* and call read() again */
            else
                return -1;
        } else if (nread == 0)
            break;              /* EOF */

        nleft -= nread;
        ptr   += nread;
    }
    return (n - nleft);         /* return >= 0 */
}

// 模拟单个客户端线程的行为
void clientThreadFunc(const char* ip, int port, int threadId) {
    int sockfd;
    struct sockaddr_in servaddr;
    
    // 1. 建立连接
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = inet_addr(ip);

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0) {
        // 连接失败直接退出该线程
        // perror("connect"); 
        close(sockfd);
        return;
    }

    // 2. 准备数据包 (避免在循环里反复 malloc/memset)
    char sendBuf[1024];
    char recvBuf[1024];
    const char* msg = "Benchmark Test Message";
    int msgLen = strlen(msg);
    
    int msgLen_net = htonl(msgLen); // 主机序→网络序
    // 拼装协议头 (4字节长度) + 协议体
    memcpy(sendBuf, &msgLen_net, 4);            
    memcpy(sendBuf + 4, msg, msgLen);      
    int totalLen = 4 + msgLen;

    // 3. 疯狂循环发送
    while (g_running) {
        // 发送数据
        if (send(sockfd, sendBuf, totalLen, 0) <= 0) {
            break; 
        }

        int len = 0;
        if (readn(sockfd, &len, 4) != 4) break;
        if (len < 0 || len > 1024) break; 
        if (readn(sockfd, recvBuf, len) != len) break;

        // 成功完成一次 Ping-Pong，计数器 +1
        g_request_count++;
    }

    close(sockfd);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("usage: ./test_qps ip port thread_count\n");
        printf("example: ./test_qps 192.168.184.128 5005 100\n");
        return -1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);
    int threadCount = atoi(argv[3]);

    printf("Start Benchmarking %s:%d with %d threads...\n", ip, port, threadCount);

    // 1. 启动压测线程
    std::vector<std::thread> threads;
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back(clientThreadFunc, ip, port, i);
    }

    // 2. 主线程负责统计 QPS
    long lastCount = 0;
    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        long currentCount = g_request_count;
        long qps = currentCount - lastCount;
        lastCount = currentCount;

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime).count();

        printf("[Time: %.2fs] Total: %ld, QPS: %ld req/sec\n", elapsed, currentCount, qps);
    }

    return 0;
}