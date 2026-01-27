// 1. client/include/AGVClient.h

#pragma once

#include <string>
#include <vector>
#include <arpa/inet.h> // for socket, connect
#include <unistd.h>    // for close
#include <stdexcept>
#include <iostream>
#include <cstring>

#include "protocol/protocol.h" // 引用 common 中的协议
#include "utils/json.hpp"      // 引用 common 中的 json

using json = nlohmann::json;

class AGVClient {
public:
    AGVClient(const std::string& ip, int port);
    ~AGVClient();

    // 连接服务器
    bool Connect();
    
    // 业务接口
    bool Login(int id, const std::string& token);
    bool RequestPath(int startX, int startY, int endX, int endY);

    // 接收线程/循环用的函数 (简单起见，我们这里设计为阻塞接收)
    // 返回收到的 JSON 数据
    json RecvResponse(MsgType& outType);

private:
    // 底层发包封装
    bool SendPacket(MsgType type, const json& body);

    // 底层收包封装
    bool RecvExact(void* buf, size_t len);

private:
    std::string serverIp_;
    int serverPort_;
    int sockFd_;
    int agvId_; // 记住自己的 ID
};


// 2. client/src/AGVClient.cpp
// 实现具体的封包、拆包逻辑。这里必须严格遵守 Server 定义的 [Len][Type][Body] 格式。
#include "AGVClient.h"

AGVClient::AGVClient(const std::string& ip, int port)
    : serverIp_(ip), serverPort_(port), sockFd_(-1), agvId_(-1) {}

AGVClient::~AGVClient() {
    if (sockFd_ > 0) {
        close(sockFd_);
    }
}

bool AGVClient::Connect() {
    sockFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFd_ < 0) {
        perror("Socket creation failed");
        return false;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(serverPort_);

    if (inet_pton(AF_INET, serverIp_.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        return false;
    }

    if (connect(sockFd_, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return false;
    }

    std::cout << "[Client] Connected to " << serverIp_ << ":" << serverPort_ << std::endl;
    return true;
}

bool AGVClient::SendPacket(MsgType type, const json& body) {
    std::string bodyStr = body.dump();
    int32_t len = static_cast<int32_t>(bodyStr.size());
    int32_t typeInt = static_cast<int32_t>(type);

    // 1. 处理网络字节序 (Host to Network Long)
    int32_t netLen = htonl(len);
    int32_t netType = htonl(typeInt);

    // 2. 发送头部 [Len (4) + Type (4)]
    // 注意：这里的发送顺序必须和 Server 的解析顺序一致
    // Server: readInt32(len) -> readInt32(type)
    if (send(sockFd_, &netLen, 4, 0) != 4) return false;
    if (send(sockFd_, &netType, 4, 0) != 4) return false;

    // 3. 发送包体
    if (len > 0) {
        if (send(sockFd_, bodyStr.data(), len, 0) != len) return false;
    }

    return true;
}

bool AGVClient::RecvExact(void* buf, size_t len) {
    size_t totalReceived = 0;
    char* charBuf = static_cast<char*>(buf);
    while (totalReceived < len) {
        ssize_t n = recv(sockFd_, charBuf + totalReceived, len - totalReceived, 0);
        if (n <= 0) return false; // Error or Closed
        totalReceived += n;
    }
    return true;
}

json AGVClient::RecvResponse(MsgType& outType) {
    // 1. 读头部
    int32_t netLen, netType;
    if (!RecvExact(&netLen, 4)) throw std::runtime_error("Server closed");
    if (!RecvExact(&netType, 4)) throw std::runtime_error("Server closed");

    // 转主机字节序
    int32_t len = ntohl(netLen);
    int32_t typeInt = ntohl(netType);
    outType = static_cast<MsgType>(typeInt);

    // 2. 读包体
    std::vector<char> buffer(len);
    if (len > 0) {
        if (!RecvExact(buffer.data(), len)) throw std::runtime_error("Read body failed");
    }

    std::string jsonStr(buffer.begin(), buffer.end());
    return json::parse(jsonStr);
}

// ================= 业务封装 =================

bool AGVClient::Login(int id, const std::string& token) {
    agvId_ = id;
    json j;
    j["id"] = id;
    j["token"] = token;
    
    if (!SendPacket(MsgType::LOGIN_REQ, j)) return false;

    MsgType type;
    json resp = RecvResponse(type);
    
    if (type == MsgType::LOGIN_RESP && resp["status"] == "OK") {
        std::cout << "[Client] Login Success! ID=" << id << std::endl;
        return true;
    }
    std::cerr << "[Client] Login Failed: " << resp.dump() << std::endl;
    return false;
}

bool AGVClient::RequestPath(int startX, int startY, int endX, int endY) {
    json j;
    j["sx"] = startX;
    j["sy"] = startY;
    j["ex"] = endX;
    j["ey"] = endY;
    j["agvId"] = agvId_; // 带上自己的 ID

    std::cout << "[Client] Requesting Path (" << startX << "," << startY 
              << ") -> (" << endX << "," << endY << ")" << std::endl;

    if (!SendPacket(MsgType::PATH_REQ, j)) return false;

    MsgType type;
    json resp = RecvResponse(type);

    if (type == MsgType::PATH_RESP) {
        if (resp["status"] == "OK") {
            auto path = resp["path"];
            std::cout << "[Client] Path Received (" << path.size() << " steps): " << std::endl;
            std::cout << path.dump() << std::endl;
            return true;
        } else {
            std::cerr << "[Client] Path Finding Failed: " << resp["reason"] << std::endl;
            return false;
        }
    }
    return false;
}

// 3. client/src/simulator_main.cpp
// 这是入口文件，可以编译成一个可执行文件 agv_client。

#include "AGVClient.h"
#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    // 默认参数
    std::string ip = "127.0.0.1";
    int port = 9999;
    int agvId = 101;

    // 简单的命令行参数解析 (可选)
    if (argc > 1) agvId = std::atoi(argv[1]);

    try {
        AGVClient client(ip, port);

        // 1. 连接
        if (!client.Connect()) {
            return -1;
        }

        // 2. 登录
        if (!client.Login(agvId, "token_123456")) {
            return -1;
        }

        // 3. 模拟业务循环
        // 场景：每隔 3 秒请求一次随机路径
        while (true) {
            // 简单的固定路径测试
            client.RequestPath(0, 0, 9, 9);
            
            // 模拟 AGV 正在移动...
            std::this_thread::sleep_for(std::chrono::seconds(3));
            
            // 可以扩展：发送心跳 MsgType::HEARTBEAT
        }

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}

client/
├── CMakeLists.txt
├── include/
│   └── AGVClient.h
├── src/
│   ├── AGVClient.cpp
│   └── simulator_main.cpp
└── scripts/ (如果你写了压测脚本)


cmake_minimum_required(VERSION 3.10)
project(AGVClient)

# C++ 标准
set(CMAKE_CXX_STANDARD 17)

# 包含路径
include_directories(${CMAKE_SOURCE_DIR}/../common/include)
include_directories(include)

# 查找所有源文件
file(GLOB CLIENT_SRCS "src/*.cpp")

# 生成可执行文件
add_executable(agv_sim ${CLIENT_SRCS})

# 链接线程库 (虽然这个简单client没用多线程，但加上保险)
target_link_libraries(agv_sim pthread)