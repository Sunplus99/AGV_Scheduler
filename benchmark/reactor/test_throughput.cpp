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
#include <string>

// 全局原子计数器，记录处理成功的请求总数
std::atomic<long> g_request_count(0);
// 控制测试运行的开关
volatile bool g_running = true;

// 封装一个保证读满 N 字节的函数
// 因为是大包(10KB)，TCP 可能会分片到达，不能假设一次 recv 就读完
ssize_t readn(int fd, void *vptr, size_t n) {
    size_t  nleft;
    ssize_t nread;
    char   *ptr;

    ptr = (char*)vptr;
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
        close(sockfd);
        return;
    }

    // 2. 准备大包数据 (10KB)
    // 构造一个 10KB 的大字符串
    std::string bigMsg(1024 * 10, 'a'); // 10KB 连续的 'a'
    int msgLen = bigMsg.length();
    
    // 准备发送缓冲区：头部(4字节) + 包体
    std::vector<char> sendBuf(4 + msgLen);
    
    // 拼装协议头 (注意：这里直接 copy int，需确保两端端序一致，本地测试没问题)
    memcpy(sendBuf.data(), &msgLen, 4);
    memcpy(sendBuf.data() + 4, bigMsg.data(), msgLen);
    
    int totalLen = 4 + msgLen;

    // 准备接收缓冲区 (复用一个足够大的 buffer)
    std::vector<char> recvBuf(msgLen); 

    // 3. 疯狂循环发送
    while (g_running) {
        // [修复点 1] 使用 sendBuf.data() 获取底层指针
        // 发送整个包（Header + Body）
        //哪怕是发送，大数据量也建议循环写，但 send 缓冲区通常足够大，这里简化处理
        if (send(sockfd, sendBuf.data(), totalLen, 0) <= 0) {
            break; 
        }

        // 接收响应 
        // 假设服务端也是回传 4字节头 + 原样内容
        int len = 0;
        
        // A. 先读头部 4 字节
        if (readn(sockfd, &len, 4) != 4) {
            break;
        }

        // 检查长度是否合理(防止发太猛服务端返回乱序或异常)
        if (len > 1024 * 100 || len < 0) { 
             break; 
        }

        // B. 再读包体 (使用循环读取 readn，确保读够 len 字节)
        // [修复点 2] 使用 recvBuf.data() 
        if (readn(sockfd, recvBuf.data(), len) != len) {
            break;
        }

        // 成功完成一次大包 Ping-Pong
        g_request_count++;
    }

    close(sockfd);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("usage: ./big_client ip port thread_count\n");
        printf("example: ./big_client 192.168.150.128 5085 1000\n");
        return -1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);
    int threadCount = atoi(argv[3]);

    printf("Start Benchmarking (10KB Packet) %s:%d with %d threads...\n", ip, port, threadCount);

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

        // 还可以顺便打印吞吐量 (MB/s)
        // QPS * 每次传输数据量(双向: 发10K + 收10K)
        double throughput = (double)qps * 2 * 10.0 / 1024.0; // MB/s

        printf("[Time: %.2fs] QPS: %ld | Throughput: %.2f MB/s\n", elapsed, qps, throughput);
    }

    return 0;
}