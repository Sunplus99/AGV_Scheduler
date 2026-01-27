bool AgvServer::SendTaskToAgv(int agvId, const agv::model::TaskRequest& task) {
    // 1. 通过 AgvMgr 找到对应的 Session
    // 注意：AgvMgr 应该是单例或者 AgvServer 持有的成员。
    // 假设你之前用的是全局单例 AgvMgr 或者成员变量 agvMgr_
    
    // 假设 AgvMgr 是全局单例 (根据你之前的代码上下文)
    auto session = AgvMgr.GetSession(agvId);
    
    if (session) {
        // 2. 定义回调 lambda (处理 AGV 的 ACK)
        auto callback = [agvId](int32_t retSeq, bool success) {
            if (success) {
                LOG_INFO("Task Dispatch ACK received from AGV %d. RefSeq: %d", agvId, retSeq);
            } else {
                LOG_WARN("Task Dispatch Timeout/Failed for AGV %d", agvId);
            }
        };

        // 3. 调用 Session 的 DispatchTask
        return session->DispatchTask(task, callback);
    } else {
        LOG_WARN("SendTask failed: AGV %d is not online.", agvId);
        return false;
    }
}



#include "AgvServer.h"
#include "config/ServerConfig.h"
#include "config/ConfigLoader.h"
#include "utils/Logger.h"
#include "model/AgvStructs.h" // 引入任务结构体
#include <csignal>
#include <thread>             // 引入线程
#include <chrono>             // 引入时间

// 全局指针
agv::AgvServer* g_server = nullptr;
// 标记 WMS 线程是否运行
std::atomic<bool> g_running{true};

// 信号处理
void signalHandler(int sig) {
    if(g_server) {
        LOG_INFO("Signal %d received. Stopping server...", sig);
        g_running = false; // 通知 WMS 线程退出
        g_server->Stop();
    }
}

// ==========================================
// 模拟 WMS (仓储管理系统) 的派单逻辑
// ==========================================
void WmsSimulatorThread(agv::AgvServer* server) {
    LOG_INFO("[WMS] Simulator started. Waiting for AGVs to connect...");
    
    // 1. 先睡一会，等待 Client 启动并登录 (比如 15秒)
    // 实际上应该轮询 GetOnlineCount()，这里简化处理
    for(int i=0; i<15; ++i) {
        if(!g_running) return;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if(!g_running) return;

    // 2. 构造一个测试任务
    LOG_INFO("[WMS] Generating Test Task...");

    agv::model::TaskRequest task;
    task.taskId = "TASK-TEST-001";
    task.targetAgvId = 101;       // 指定给 101 号车
    task.targetPos = {50, 50};    // 目标去 (50, 50)
    task.targetAct = 1;           // 假设 1 代表 "PickUp"
    task.priority = 10;
    
    // 3. 下发任务
    // 尝试发送，如果 AGV 还没上线，可以重试几次
    for (int retry = 0; retry < 5; ++retry) {
        if (!g_running) break;

        if (server->SendTaskToAgv(101, task)) {
            LOG_INFO("[WMS] Task %s dispatched successfully to AGV 101", task.taskId.c_str());
            break;
        } else {
            LOG_WARN("[WMS] AGV 101 not ready. Retrying in 2s...");
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    // 4. (可选) 持续生成任务...
    // while(g_running) { ... }
}

int main(int argc, char* argv[]) {

    LOG_INFO("========== AGV Server Booting Up ==========");

    // 1. 加载配置
    std::string configPath = "./config.json";
    if(argc > 1) configPath = argv[1];

    agv::config::ServerConfig cfg;
    if (!agv::config::ConfigLoader::Load(configPath, cfg)) {
        LOG_WARN("Failed config load. Using defaults.");
    }

    try {
        // 2. 初始化 Server
        agv::AgvServer server(cfg);
        g_server = &server;

        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);

        // 3. 【关键】启动 WMS 模拟线程
        // 注意：必须在 server.Start() 之前启动，或者 server.Start() 是非阻塞的。
        // 通常 server.Start() 是阻塞死循环，所以要用线程。
        std::thread wmsThread(WmsSimulatorThread, &server);

        // 4. 启动服务器主循环 (阻塞在此)
        server.Start(); 

        // 5. 服务器退出后，等待 WMS 线程结束
        if(wmsThread.joinable()) {
            wmsThread.join();
        }

    } catch (const std::exception& e){
        LOG_ERROR("Crash: %s", e.what());
        return 1;
    }

    g_server = nullptr;
    LOG_INFO("========== AGV Server Shutdown ==========");
    return 0; 
}