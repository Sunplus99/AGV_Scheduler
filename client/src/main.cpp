

#include "protocol/AgvCodec.h"
#include "model/AgvStructs.h"
#include "protocol/MsgType.h"
#include "protocol/AgvMessage.h"

#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <map>
#include <chrono>
#include <random>
#include <atomic>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>

// 引入 json
#include "utils/json.hpp"
using json = nlohmann::json;
using namespace agv::protocol;
using namespace agv::model;


// ==========================================
// 1. 上帝视角 (模拟雷达/全局位置)
// ==========================================
struct GlobalWorld {
    // 记录所有 AGV 的当前位置: <agvId, Point>
    std::map<int, Point> positions;
    std::mutex mtx;

    // 更新位置
    void UpdatePos(int id, Point p) {
        std::lock_guard<std::mutex> lock(mtx);
        positions[id] = p;
    }

    // 模拟雷达探测：检查某个点是否被占用
    bool IsOccupied(int myId, Point target) {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& [id, pos] : positions) {
            if (id == myId) continue; // 不跟自己撞
            // 简单碰撞检测：坐标完全重合就算撞
            if (pos.x == target.x && pos.y == target.y) {
                return true; 
            }
        }
        return false;
    }
} g_world;

// ==========================================
// 2. 模拟 AGV 类
// ==========================================
class SimulatedAgv {
public:
    SimulatedAgv(int id, std::string ip, int port, Point startPos) 
        : id_(id), serverIp_(ip), serverPort_(port), currentPos_(startPos) {
            g_world.UpdatePos(id_, currentPos_);
    }

    void Run() {
        if (!Connect()) return;

        // 登录 (同步阻塞，超时较长) 
        SetTimeout(3000); 

        SendLogin();

        // 接收登录响应 (复用通用收包逻辑)
        MsgHead head;
        std::string body;
        
        // 尝试读取，如果返回 false 说明超时或断开
        if (RecvPacket(head, body)) {
             if (head.type == (int32_t)MsgType::LOGIN_RESP) {
                 try {
                     json j = json::parse(body);
                     if (j["success"]) {
                         token_ = j["token"]; // 暂时没用，但协议里有
                         isLogged_ = true;
                         printf("[AGV-%d] Login Success! Token: %s\n", id_, j["token"].get<std::string>().c_str());
                     } else {
                         printf("[AGV-%d] Login Refused: %s\n", id_, j["message"].get<std::string>().c_str());
                         close(sock_);
                         return;
                     }
                 } catch(...) {
                     printf("[AGV-%d] Login Response Parse Error.\n", id_);
                     close(sock_);
                     return;
                 }
             } else {
                 printf("[AGV-%d] Unexpected MsgType during login: %d\n", id_, head.type);
                 close(sock_);
                 return;
             }
        } else {
            printf("[AGV-%d] Login Timeout or Connection Lost.\n", id_);
            close(sock_);
            return;
        }

        // 改回 100ms 超时，实现 Time-Slicing
        SetTimeout(100); 

        while (running_) {
            HandleNetwork(); // 处理网络
            UpdateLogic();   // 处理移动
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟 CPU 休息 ; 防止空转占满 CPU
        }

        close(sock_);
        printf("[AGV-%d] Shutdown.\n", id_);
    }

private:
    int id_;
    std::string serverIp_;
    int serverPort_;
    int sock_ = -1;
    
    // 状态
    bool running_ = true;
    bool isLogged_ = false;
    Point currentPos_;
    
    // 任务相关
    std::string currentTaskId_ = "";
    Point currentTaskTarget_ = {0, 0};
    std::vector<Point> path_; // 当前领到的任务路径
    int pathIndex_ = 0;       // 走到路径的第几步了
    bool isWorking_ = false;  // 是否在执行任务

    std::string token_ = "";

    // 计时器  ms
    std::chrono::steady_clock::time_point lastHeartbeatTime_ =  std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastMoveTime_ =
     std::chrono::steady_clock::now();
    int32_t txSeq_ = 0;

private:

    // 设置 Socket 超时时间的辅助函数
    void SetTimeout(int ms) {
        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        if(setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
            perror("Setsockopt failed");
        }
    }

    
    // 返回值定义:  0 : 成功 (读满了 targetSize); -1 : 错误/断开 (需要关闭连接);  1 : 超时 (仅在 nothing read 时返回，表示本次无数据，可以重试)
    int RecvFixedBytes(char* buf, int targetSize, bool isBlockMode = false) {
        int totalReceived = 0;
        
        while (totalReceived < targetSize) {
            
            int n = recv(sock_, buf + totalReceived, targetSize - totalReceived, 0);

            if (n > 0) {
                totalReceived += n;
            } 
            else if (n == 0) {
                printf("[AGV] Peer Closed.\n");
                running_ = false;
                return -1; // 对端关闭
            } 
            else { // n < 0
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    if (totalReceived == 0 && !isBlockMode) {
                        // 还没读到任何数据，且允许超时（通常是读包头起始时）
                        return 1; 
                    } else {
                        // 已经读了一部分了，或者正在读包体，结果超时了！
                        // 简单处理：视为错误，断开连接，防止流错位。
                        printf("[AGV] Fatal: Partial read timeout! Stream corrupted.\n");
                        running_ = false;
                        return -1;
                    }
                }
                perror("Recv Error");
                running_ = false;
                return -1; // 其他错误
            }
        }
        return 0; // 成功
    }

    // 收包函数，返回: true=成功读到一个包, false=超时或断开
    bool RecvPacket(MsgHead& outHead, std::string& outBody) {
        // A. 读取包头(允许超时)
        int ret = RecvFixedBytes((char*)&outHead, sizeof(MsgHead), false);
        
        if (ret == 1) {
            return false; // 超时 (没数据)，直接返回让外层继续跑心跳
        }
        if (ret == -1) {
            return false; // 出错或断开
        }

        // 转主机序
        outHead.len  = ntohl(outHead.len);
        outHead.type = ntohl(outHead.type);
        outHead.seq  = ntohl(outHead.seq);

        // B. 读取包体（不允许超时）
        if (outHead.len > 0) {
            // 安全检查：防止非法包导致内存爆炸
            if (outHead.len > MAX_MSG_BODY_LEN) { 
                printf("[AGV] Msg too large! len=%d\n", outHead.len);
                running_ = false;
                return false;
            }

            std::vector<char> buf(outHead.len);
            
            /*
            头是必须有的（但可以迟到），体是必须紧跟着头的（不可迟到）。
            设 true, 强制读完
            */
            ret = RecvFixedBytes(buf.data(), outHead.len, true);
            
            if (ret != 0) { 
                // 无论是 -1 (错误) 还是 1 (超时)，在读包体阶段都是致命的
                printf("[AGV] Failed to read body.\n");
                return false; 
            }

            outBody.assign(buf.data(), buf.size());

        } else {
            outBody.clear();
        }

        return true; // 完整包读取成功
    }


    bool Connect() {
        sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ < 0) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(serverPort_);
        inet_pton(AF_INET, serverIp_.c_str(), &addr.sin_addr);

        if (connect(sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            printf("[AGV-%d] Connect Failed.\n", id_);
            return false;
        }
        printf("[AGV-%d] Connected.\n", id_);
        return true;
    }

    void SendPacket(MsgType type, const json& body, int32_t refSeq = 0) {
        std::string bodyStr = body.dump();
        
        MsgHead head;
        head.len = htonl(bodyStr.size());
        head.type = htonl((int32_t)type);
        head.seq = htonl(++txSeq_); // 自身发送序列号
        
        std::vector<char> buf(sizeof(MsgHead) + bodyStr.size());
        memcpy(buf.data(), &head, sizeof(MsgHead));
        memcpy(buf.data() + sizeof(MsgHead), bodyStr.c_str(), bodyStr.size());

        send(sock_, buf.data(), buf.size(), 0);
    }


    void SendLogin() {
        json j;
        j["agvId"] = id_;
        j["password"] = "123456";
        j["version"] = "1.0.0";
        j["initialPos"] = { {"x", currentPos_.x}, {"y", currentPos_.y} };
        SendPacket(MsgType::LOGIN_REQ, j);
        printf("[AGV-%d] Sent Login.\n", id_);
    }

    void SendHeartbeat() {
        json j;
        j["agvId"] = id_;
        j["status"] = isWorking_ ? AgvStatus::MOVING : AgvStatus::IDLE;
        j["currentPos"] = { {"x", currentPos_.x}, {"y", currentPos_.y} };
        j["battery"] = 100.0;

        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
        j["timestamp"] = ms.count();
        SendPacket(MsgType::HEARTBEAT, j);
    }

    void SendTaskReport(AgvStatus status, double progress, int32_t ackSeq = 0) {
        json j;
        j["taskId"] = currentTaskId_;
        j["agvId"] = id_;
        j["status"] = status; 
        j["currentPos"] = { {"x", currentPos_.x}, {"y", currentPos_.y} };
        j["progress"] = progress;
        j["refSeq"] = ackSeq;
        SendPacket(MsgType::TASK_REPORT, j);

        if (ackSeq > 0) {
            printf("[AGV-%d] > Sent ACK (RefSeq=%d) for Task %s.\n", id_, ackSeq, currentTaskId_.c_str());
        }
    }

    void RequestNewPath(Point end) {
        json j;
        j["mapId"] = 1;
        j["start"] = { {"x", currentPos_.x}, {"y", currentPos_.y} };
        j["end"] = { {"x", end.x}, {"y", end.y} };
        j["allowReplan"] = true;
        SendPacket(MsgType::PATH_REQ, j);

        printf("[AGV-%d] Requesting path: (%d,%d) -> (%d,%d)\n",
               id_, currentPos_.x, currentPos_.y, end.x, end.y);
    }

    // --- 核心逻辑循环 ---
    void UpdateLogic() {
        // 逻辑判断用 steady_clock (为了防时间跳变)
        auto now = std::chrono::steady_clock::now();

        // 1. 发送心跳 (每1秒)
        auto diffHb = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeatTime_).count();
        if (isLogged_ && (diffHb > 1000)) {
            SendHeartbeat();
            lastHeartbeatTime_ = now;
        }

        // 2. 模拟移动 (每500ms走一步)
        auto diffMove = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime_).count();
        if (isWorking_ && !path_.empty() && (diffMove> 500)) {
            // 获取下一步要去的地方
            if (pathIndex_ < (int)path_.size()) {
                Point nextStep = path_[pathIndex_];

                // 【上帝视角避障】
                // 模拟雷达：看一眼下一步有没有别的车
                if (g_world.IsOccupied(id_, nextStep)) {
                    // 发现障碍！停止移动，请求服务器重规划
                    // 目标点是路径的终点
                    RequestNewPath(path_.back());
                    
                    lastMoveTime_ = now;
                    return; 
                }

                // 无障碍，走动
                currentPos_ = nextStep;
                g_world.UpdatePos(id_, currentPos_); // 更新上帝视角的坐标
                ++pathIndex_;
                lastMoveTime_ = now;

                // 汇报进度
                double progress = (double)pathIndex_ / path_.size();
                SendTaskReport(AgvStatus::MOVING, progress); // 2=BUSY
                printf("[AGV-%d] Moved to (%d,%d)\n", id_, currentPos_.x, currentPos_.y);
            } else {
                // 走完了
                isWorking_ = false;
                path_.clear();
                SendTaskReport(AgvStatus::IDLE, 1.0); // 
                printf("[AGV-%d] Task Completed.\n", id_);
            }
        }
    }

    void HandleNetwork() {
        MsgHead head;
        std::string body;
        
        if (!RecvPacket(head, body)) {
            return; 
        }

        try {
            json j = json::parse(body);
            ProcessMessage((MsgType)head.type, head.seq, j);
        } catch (...) {
            printf("[AGV-%d] JSON Parse Error.\n", id_);
        }
       
    }

    void ProcessMessage(MsgType type, int32_t seq, const json& j) {
        switch (type) {
            case MsgType::TASK_REQUEST: {
                // 1. 更新状态
                currentTaskId_ = j["taskId"];
                currentTaskTarget_ = { j["targetPos"]["x"], j["targetPos"]["y"] };
                
                printf("[AGV-%d] Received Task [%s] -> Go to (%d, %d)\n", 
                       id_, currentTaskId_.c_str(), currentTaskTarget_.x, currentTaskTarget_.y);

                // 2. 立即回复 ACK (TaskReport)
                SendTaskReport(AgvStatus::IDLE, 0.0 , seq); 

                // 3. 发起寻路请求 (参数是任务目的地)
                RequestNewPath(currentTaskTarget_);
                printf("[AGV-%d] Requesting Path to target point(%d,%d)...\n", id_, currentTaskTarget_.x, currentTaskTarget_.y);
                break;
            }

            case MsgType::PATH_RESP: {
                if (j["success"]) {
                    path_.clear();
                    for (auto& p : j["pathPoints"]) {
                        path_.push_back({p["x"], p["y"]});
                    }
                    pathIndex_ = 0;

                    // 检查路径是否为空（已经在目标位置）
                    if (path_.empty()) {
                        // 已经在目标位置，立即完成任务
                        printf("[AGV-%d] Already at target! Task completed immediately.\n", id_);
                        SendTaskReport(AgvStatus::IDLE, 1.0);
                        isWorking_ = false;
                    } else {
                        // 有路径，开始执行
                        isWorking_ = true;
                        printf("[AGV-%d] Path Planned! Steps: %lu. Starting...\n", id_, path_.size());
                    }
                } else {
                    printf("[AGV-%d] Path Planning Failed! Blocked.\n", id_);
                    // RequestNewPath(currentTaskTarget_);
                }
                break;
            }
            
            default: break;
        }
    }
};

// ==========================================
// 3. Main 入口
// ==========================================
int main(int argc, char* argv[]) {
    // 从命令行参数读取 AGV 数量，默认 10 辆
    int agvCount = 10;
    if (argc > 1) {
        agvCount = std::atoi(argv[1]);
        if (agvCount <= 0 || agvCount > 100) {
            printf("Invalid AGV count: %d. Using default: 10\n", agvCount);
            agvCount = 10;
        }
    }

    printf("Starting %d AGV simulators...\n", agvCount);

    // 启动多辆车模拟
    std::vector<std::thread> threads;

    // 动态生成 AGV 配置（网格分布）
    int gridSize = static_cast<int>(std::ceil(std::sqrt(agvCount)));
    int mapSize = 50;  // 假设地图大小为 50x50
    int cellSize = (mapSize - 2) / gridSize;

    for (int i = 0; i < agvCount; ++i) {
        int agvId = 101 + i;
        int gx = i % gridSize;
        int gy = i / gridSize;
        int x = 1 + gx * cellSize + cellSize / 2;
        int y = 1 + gy * cellSize + cellSize / 2;

        threads.emplace_back([agvId, x, y]() {
            SimulatedAgv agv(agvId, "127.0.0.1", 8888, {x, y});
            agv.Run();
        });

        // 错开启动时间，避免 Server 瞬间压力过大
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 等待所有线程
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    return 0;
}