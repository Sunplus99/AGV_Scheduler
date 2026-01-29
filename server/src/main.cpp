#include "AgvServer.h"
#include "config/ServerConfig.h"
#include "config/ConfigLoader.h"
#include "utils/Logger.h"
#include <csignal>
#include <thread>
#include <atomic>
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
    LOG_INFO("[WMS] System warming up... Waiting for AGVs to login (10s)...");

    // 1. 预热阶段：等待 Client 启动并完成登录
    // 实际生产中可以调用 WorldMgr.GetOnlineCount() 来判断
    for(int i = 0; i < 10; ++i) {
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

    for (int i = 0; i < taskCount; ++i) {
        agv::model::Point target = gridMap.GetRandomWalkablePoint();
        agv::model::ActionType action = static_cast<agv::model::ActionType>(i % 3);  // 循环使用不同动作

        std::string taskId = TaskMgr.AddTask(target, action);
        LOG_INFO("[WMS] >>> Order %d/%d Created: ID=%s, Target=(%d,%d)",
                 i + 1, taskCount, taskId.c_str(), target.x, target.y);

        // 任务间隔 100ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO("[WMS] All test orders dispatched. Entering Monitor Mode...");

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
    // 用法: ./AgvServer [config_path] [log_level]
    // 示例: ./AgvServer ./config.json WARN
    std::string configPath = "./config.json";
    LogLevel logLevel = INFO;  // 默认 INFO 级别

    if (argc > 1) {
        configPath = argv[1];
    }
    if (argc > 2) {
        std::string levelStr = argv[2];
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
    LOG_INFO("Log Level: %s", argv[2] ? argv[2] : "INFO");

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