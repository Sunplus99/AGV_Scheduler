#include "AgvServer.h"
#include "config/ServerConfig.h"
#include "config/ConfigLoader.h"
#include "utils/Logger.h"
#include <csignal>
#include <thread>
#include <atomic>
#include <set>
#include "manager/TaskManager.h"
#include "manager/WorldManager.h"
#include "chrono"

//  全局指针，用于在信号函数中访问 AgvServer实例
agv::AgvServer* g_server = nullptr;
//  全局运行标志
std::atomic<bool> g_running{true};

// 信号处理函数
void signalHandler(int sig) {
    LOG_INFO("Signal %d received. Stopping server...", sig);

    g_running = false;  // 辅助线程退出

    if(g_server) {
        g_server->Stop();
    }
}

void WmsThreadFunc() {
    LOG_INFO("[WMS] Simulator Thread Started.");
    LOG_INFO("[WMS] System warming up... Waiting for AGVs to login (15s)...");

    // 1. 预热阶段：等待 Client 启动并完成登录
    // 实际生产中可以调用 WorldMgr.GetOnlineCount() 来判断
    for(int i = 0; i < 15; ++i) {
        if(!g_running) return; // 随时响应退出信号
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if(!g_running) return;

    LOG_INFO("[WMS] Start Dispatching Tasks...");

    // 2. 动态生成任务（从地图中随机选择可通行点作为目标）
    const auto& gridMap = agv::manager::WorldManager::Instance().GetGridMap();

    // 根据在线 AGV 数量生成任务（每辆车 2-3 个任务）
    int onlineCount = agv::manager::WorldManager::Instance().GetAllAgvs().size();
    int taskCount = onlineCount * 2;  // 每辆车平均 2 个任务

    LOG_INFO("[WMS] Generating %d tasks for %d AGVs...", taskCount, onlineCount);

    std::vector<agv::model::Point> usedTargets;  // 记录已使用的目标点（用于距离检查）
    const int MIN_DISTANCE = 4;  // 目标点之间的最小距离（曼哈顿距离）- 降低要求

    // 辅助函数：计算曼哈顿距离
    auto manhattanDist = [](const agv::model::Point& a, const agv::model::Point& b) {
        return std::abs(a.x - b.x) + std::abs(a.y - b.y);
    };

    int actualTaskCount = 0;  // 实际生成的任务数

    for (int i = 0; i < taskCount; ++i) {
        agv::model::Point target;
        int retryCount = 0;
        bool isValid = false;

        // 确保目标点：1.不在墙里（GetRandomWalkablePoint保证） 2.不被AGV占用 3.距离其他目标点足够远
        // 移除可达性验证（太慢），交给调度算法处理
        do {
            target = gridMap.GetRandomWalkablePoint();
            retryCount++;

            // 检查是否被AGV占用
            if (agv::manager::WorldManager::Instance().IsOccupied(target, -1)) {
                continue;
            }

            // 检查与已有目标点的距离
            bool tooClose = false;
            for (const auto& existingTarget : usedTargets) {
                if (manhattanDist(target, existingTarget) < MIN_DISTANCE) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) {
                continue;
            }

            // 通过所有检查
            isValid = true;
            break;

        } while (retryCount < 100);  // 降低重试次数

        if (!isValid) {
            LOG_WARN("[WMS] Failed to find valid target after 100 retries, skipping task %d.", i + 1);
            continue;  // 跳过这个任务
        }

        usedTargets.push_back(target);

        agv::model::ActionType action = static_cast<agv::model::ActionType>(i % 3);

        std::string taskId = TaskMgr.AddTask(target, action);
        LOG_INFO("[WMS] >>> Order %d/%d Created: ID=%s, Target=(%d,%d)",
                 i + 1, taskCount, taskId.c_str(), target.x, target.y);

        actualTaskCount++;  // 记录实际生成数

        // 任务间隔 100ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO("[WMS] Task generation completed. Generated: %d/%d tasks.", actualTaskCount, taskCount);

    // 告知 TaskManager 实际生成的任务数（而不是预期数）
    TaskMgr.SetTotalTaskCount(static_cast<uint64_t>(actualTaskCount));

    // 3. 监控阶段
    // 保持线程存活，防止主进程退出。这里可以打印一些系统状态监控日志。
    while(g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        // 可选：打印当前任务队列长度
        // LOG_DEBUG("[WMS] Monitor: System is running...");
    }
    
    LOG_INFO("[WMS] Simulator Thread Exiting...");
}



int main(int argc, char* argv[]) {

    // ========== 解析命令行参数 ==========
    // 用法: ./AgvServer  [log_level]
    // 示例: ./AgvServer WARN
    std::string configPath = "./config.json";  // 相对于当前工作目录,在 bin/ 目录运行，读取的是 bin/config.json
    LogLevel logLevel = INFO;  // 默认 INFO 级别

    if (argc > 1) {
        std::string levelStr = argv[1];
        if (levelStr == "DEBUG") logLevel = DEBUG;
        else if (levelStr == "INFO") logLevel = INFO;
        else if (levelStr == "WARN") logLevel = WARN;
        else if (levelStr == "ERROR") logLevel = ERROR;
        else if (levelStr == "FATAL") logLevel = FATAL;
        else {
            fprintf(stderr, "Invalid log level: %s. Using INFO.\n", levelStr.c_str());
            fprintf(stderr, "Valid levels: DEBUG, INFO, WARN, ERROR, FATAL\n");
        }
    }
    
    // 设置日志级别
    Logger::Instance().SetLevel(logLevel);

    // ========== 初始化日志系统 ==========
    // 启用文件日志输出（异步双缓冲，不阻塞主线程）
    // 日志文件路径：./logs/agv_server.log
    if (!Logger::Instance().Open("./logs/agv_server.log")) {
        fprintf(stderr, "Failed to open log file. Logging to console only.\n");
    } else {
        LOG_INFO("Log file opened: ./logs/agv_server.log");
    }

    LOG_INFO("========== AGV Server Booting Up ==========");
    LOG_INFO("Log Level: %s", argc > 1 ? argv[1] : "INFO");

    // 配置文件
    agv::config::ServerConfig cfg;
    if (!agv::config::ConfigLoader::Load(configPath, cfg)) {
        LOG_WARN("Failed to load config from '%s'. Using default hardcoded settings.", configPath.c_str());
    } else {
        LOG_INFO("Config loaded successfully. IP=%s Port=%d", cfg.ip.c_str(), cfg.port);
    }
    
    try {
        agv::AgvServer server(cfg);
        g_server = &server;

        // 注册信号捕获
        signal(SIGINT, signalHandler);  // Ctrl+C
        signal(SIGTERM, signalHandler); // kill

        std::thread wmsThread(WmsThreadFunc);

        LOG_INFO("Server EventLoop Starting...");
        server.Start();

    } catch (const std::exception& e){
        LOG_ERROR("Server crashed with exception: %s", e.what());
        return 1;
    }

    g_server = nullptr;
    LOG_INFO("========== AGV Server Shutdown ==========");
    return 0; 
}