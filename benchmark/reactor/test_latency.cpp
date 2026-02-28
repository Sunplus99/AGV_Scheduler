#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iomanip>

// 配置目标服务器
const char* SERVER_IP = "192.168.184.128"; 
const int SERVER_PORT = 5005;
const int REQUESTS_COUNT = 10000; 
const int MSG_BODY_SIZE = 64; 

// 打包协议 (4字节长度 + 内容)
std::string pack_message(const std::string& body) {
    int len = body.size();
    std::string pkg;
    pkg.resize(4 + len);
    
    int msgLen_net = htonl(len);

    // 写入头部
    memcpy(&pkg[0], &msgLen_net, 4);
    // 写入包体
    memcpy(&pkg[4], body.data(), len);
    
    return pkg;
}

int main() {

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        return -1;
    }

    std::cout << "Connected. Start measuring latency for " << REQUESTS_COUNT << " requests..." << std::endl;

    // 准备带协议头的数据包
    std::string raw_body(MSG_BODY_SIZE, 'a');
    std::string packet = pack_message(raw_body);

    std::vector<char> recv_buf(1024);
    std::vector<double> latencies; 
    latencies.reserve(REQUESTS_COUNT);

    for (int i = 0; i < REQUESTS_COUNT; ++i) {
        auto start = std::chrono::high_resolution_clock::now();

        // 1. 发送 (发送完整的协议包)
        ssize_t sent = send(sock, packet.data(), packet.size(), 0);
        if (sent != (ssize_t)packet.size()) {
            std::cerr << "Send failed or partial send." << std::endl;
            break;
        }

        // 2. 接收 (Server 回射的数据也应该有头，我们这里简单处理，只要收到数据就算结束)
        // 真实场景应该循环接收直到收满预期长度，但延迟测试为了快，读到响应即可
        ssize_t n = recv(sock, recv_buf.data(), recv_buf.size(), 0);

        auto end = std::chrono::high_resolution_clock::now();
        
        if (n <= 0) {
            std::cerr << "Server closed connection (recv returned " << n << ")" << std::endl;
            break;
        }

        std::chrono::duration<double, std::micro> diff = end - start;
        latencies.push_back(diff.count());
        
        // 可选：防止把 Server 压得太死，模拟真实间隔（测极限延迟可以注释掉）
        // usleep(50); 
    }

    close(sock);

    if (latencies.empty()) return 0;

    std::sort(latencies.begin(), latencies.end());

    double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
    double avg = sum / latencies.size();
    
    auto get_percentile = [&](double p) {
        int index = std::round(latencies.size() * p) - 1;
        if (index < 0) index = 0;
        return latencies[index];
    };

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n======== Latency Report (Microseconds) ========" << std::endl;
    std::cout << "Total Requests: " << latencies.size() << std::endl;
    std::cout << "Min: " << latencies.front() << " us" << std::endl;
    std::cout << "Avg: " << avg << " us" << std::endl;
    std::cout << "P50: " << get_percentile(0.50) << " us" << std::endl;
    std::cout << "P90: " << get_percentile(0.90) << " us" << std::endl;
    std::cout << "P95: " << get_percentile(0.95) << " us" << std::endl;
    std::cout << "P99: " << get_percentile(0.99) << " us" << std::endl;
    std::cout << "Max: " << latencies.back() << " us" << std::endl;
    std::cout << "===============================================" << std::endl;

    return 0;
}
