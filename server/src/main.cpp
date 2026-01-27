#include "AgvServer.h"
#include "config/ServerConfig.h"
#include "config/ConfigLoader.h"
#include "utils/Logger.h"
#include <csignal>
#include <thread>
#include <atomic>
#include "manager/TaskManager.h"
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

    // 2. 生产任务 (模拟业务系统下单)
    
    // --- 场景 A：正常任务 ---
    // 让 AGV 去坐标 (10, 0) 取货
    // TaskMgr 会自动生成 TaskID，存入 Pending 队列，并尝试调度
    {
        agv::model::Point target = {10, 0};
        std::string taskId = TaskMgr.AddTask(target, agv::model::ActionType::LIFT_UP);
        LOG_INFO("[WMS] >>> Order Created: ID=%s, Target=(%d,%d), Type=LIFU_UP", taskId.c_str(), target.x, target.y);
    }

    // 模拟业务间隔 (500ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // --- 场景 B：制造冲突/对向行驶 ---
    // 让另一辆车去 (0, 0) 送货。如果 101 和 102 在一条线上，这里会触发避障或调度锁
    {
        agv::model::Point target = {0, 0};
        std::string taskId = TaskMgr.AddTask(target, agv::model::ActionType::LIFT_UP);
        LOG_INFO("[WMS] >>> Order Created: ID=%s, Target=(%d,%d), Type=DELIVER", taskId.c_str(), target.x, target.y);
    }

    // --- 场景 C：第三个任务 ---
    {
        agv::model::Point target = {5, 5};
        std::string taskId = TaskMgr.AddTask(target, agv::model::ActionType::CHARGE);
        LOG_INFO("[WMS] >>> Order Created: ID=%s, Target=(%d,%d), Type=CHARGE", 
                 taskId.c_str(), target.x, target.y);
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

    LOG_INFO("========== AGV Server Booting Up ==========");

    // 配置文件
    std::string configPath = "./config.json";
    if(argc > 1) configPath = argv[1];

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