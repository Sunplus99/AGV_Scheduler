#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/resource.h> // 用于修改文件描述符限制
#include <thread>
#include <chrono>

// 配置目标服务器
const char* SERVER_IP = "192.168.184.128"; // 【请修改为你的 Server IP】
const int SERVER_PORT = 5005;
const int CONNS_COUNT = 10000;             // 目标连接数

void set_resource_limit() {
    struct rlimit rl;
    // 获取当前限制
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        perror("getrlimit");
        exit(1);
    }

    std::cout << "Original Limit: " << rl.rlim_cur << std::endl;

    // 设置为最大值 (或者至少比 CONNS_COUNT 大)
    rl.rlim_cur = rl.rlim_max = 65535;
    
    if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
        perror("setrlimit failed. Please run with sudo or check ulimit -n");
        // 继续尝试，如果失败后续 connect 会报错
    } else {
        std::cout << "New Limit set to: " << rl.rlim_cur << std::endl;
    }
}

int main(int argc, char* argv[]) {
    // 1. 先把系统限制提上去，否则连到 1024 就崩了
    set_resource_limit();

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    std::vector<int> connections;
    connections.reserve(CONNS_COUNT);

    std::cout << "Start connecting to " << SERVER_IP << ":" << SERVER_PORT 
              << ", target: " << CONNS_COUNT << " connections..." << std::endl;

    for (int i = 0; i < CONNS_COUNT; ++i) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("socket error");
            break;
        }

        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Connect failed at index " << i << ": " << strerror(errno) << std::endl;
            
            // 常见错误处理：端口耗尽
            if (errno == EADDRNOTAVAIL) {
                std::cerr << "Hint: Port exhaustion. Increase net.ipv4.ip_local_port_range" << std::endl;
            }
            close(sock);
            break; // 连接失败通常意味着严重问题，直接退出循环
        }

        // 保存 socket fd，防止被关闭
        connections.push_back(sock);

        if ((i + 1) % 1000 == 0) {
            std::cout << "Established " << (i + 1) << " connections..." << std::endl;
            // 稍微停顿一下，防止瞬间 SYN Flood 把服务器 Accept 队列冲爆
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    std::cout << "Done! Total success: " << connections.size() << std::endl;
    std::cout << "Client is sleeping to hold connections. Press Ctrl+C to exit." << std::endl;

    // 死循环，保持连接不断开
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    return 0;
}
