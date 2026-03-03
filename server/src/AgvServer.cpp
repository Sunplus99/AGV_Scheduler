#include "AgvServer.h"
#include "session/AgvManager.h"
#include "manager/TaskManager.h"
#include "manager/WorldManager.h"
#include "utils/Logger.h"



namespace agv{

using namespace model;
using namespace protocol;


AgvServer::AgvServer(const config::ServerConfig& config)
    : config_(config),
      tcpServer_(std::make_unique<myreactor::TcpServer>(config.ip, config.port, config.ioThreadnum)),
      workerPool_(std::make_unique<myreactor::ThreadPool>(config.workerThreadnum)),
      disPatcher_()
{
    Init();    
    LOG_INFO("AgvServer constructed. [IP:%s Port:%d IOs:%d Workers:%d]", config.ip.c_str(), config.port, config.ioThreadnum, config.workerThreadnum);
}

AgvServer::~AgvServer() {
    Stop();
}

void AgvServer::Init() {
    SetupInfra();
    InitSysRes();
    SetupNecbs();
    RegisBnhad();
}

// 1st. 基础设置 (依赖注入)
void AgvServer::SetupInfra() {
    TaskMgr.Init(workerPool_.get());
}

// 2nd. 系统资源 (地图加载、未来数据库连接等)
void AgvServer::InitSysRes() {
    bool res = false;

    switch (config_.map.type) {
        case config::MapType::DEFAULT:
            LOG_INFO("Loading Default Map...");
            res = WorldMgr.Init();
            break;
        
        case config::MapType::FILE:
            LOG_INFO("Loading Map from File: %s", config_.map.path.c_str());
            res = WorldMgr.Init(config_.map.path);
            break;

        case config::MapType::RANDOM:
            LOG_INFO("Loading Random Map...");
            res = WorldMgr.Init(config_.map.width, config_.map.height, config_.map.obstacleRatio);
            break;
    }
    
    if (!res) {
        LOG_FATAL("[Init] Failed to initialize World Manager! MapType: %d", (int)config_.map.type);
        
        throw std::runtime_error("System Resource Initialization Failed");
    }

    LOG_INFO("[Init] World Map initialized successfully.");
    
}

// 3rd. 底层回调
void AgvServer::SetupNecbs(){
    tcpServer_->setnewconncb( [this](const spConnection& conn){
        AgvMgr.OnNewConn(conn,*(this->workerPool_));  
    });

    tcpServer_->setclosecb( [](const spConnection& conn){
        AgvMgr.OnClose(conn);
    });

    tcpServer_->setconntimeoutcb( [](const spConnection& conn){
        LOG_WARN("Connection Timeout (TCP Heartbeat missing): %s:%d", conn->ip().c_str(), conn->port());
    });

    tcpServer_->setonmesscb( [this](const spConnection& conn, myreactor::Buffer* buf){
        this->OnTcpMessage(conn, buf);
    });
    tcpServer_->setTickcb( [this](){ 
        AgvMgr.CheckAllTimeouts(this->config_.rpcTimeoutMs);
    });

}

// 4th. 业务层注册
void AgvServer::RegisBnhad() {
    disPatcher_.registerHandler<LoginRequest>(
        MsgType::LOGIN_REQ,
        [](const spConnection& conn, const LoginRequest& req, int32_t seq){
            // if(auto sess = AgvMgr.GetSession(conn)){ // 旧，有锁
            if (auto sess = conn->getContext<session::AgvSession>()) {
                sess->HandleLogin(req, seq);
            }else{
                LOG_ERROR("Session Lost during Login processing");
            }
        }
    );

    disPatcher_.registerHandler<Heartbeat>(
        MsgType::HEARTBEAT,
        [](const spConnection& conn, const Heartbeat& hb, int32_t seq){
            if(auto sess = conn->getContext<session::AgvSession>())
                sess->HandleHbeat(hb, seq);
        }
    );

    disPatcher_.registerHandler<TaskReport>(
        MsgType::TASK_REPORT,
        [](const spConnection& conn, const TaskReport& msg, int32_t seq){
            if(auto sess = conn->getContext<session::AgvSession>())
                sess->HandleTRepo(msg, seq);
        }
    );

    disPatcher_.registerHandler<PathRequest>(
        MsgType::PATH_REQ,
        [](const spConnection& conn, const PathRequest& req, int32_t seq){
            if(auto sess = conn->getContext<session::AgvSession>())
                sess->HandlePRequ(req, seq);
        }
    );
}

void AgvServer::Start() {
    LOG_INFO("AgvServer Starting...");
    tcpServer_->setTimeOut(config_.tcpTimeoutSec); // 网络层超时，单位 s 
    workerPool_->start();  // 先启动计算资源
    tcpServer_->start();   // 再启动网络监听
    LOG_INFO("AgvServer Started Listening on Port %d", tcpServer_->port());
}

void AgvServer::Stop() {
    LOG_INFO("AgvServer Stopping...");

    // ===== 性能统计汇总 =====
    if (qpsStarted_.load(std::memory_order_relaxed)) {
        double elapsedSec = (myreactor::Timestamp::now().usSinceEpoch()
                             - qpsStartTime_.usSinceEpoch()) / 1000000.0;
        uint64_t total = totalRequests_.load(std::memory_order_relaxed);
        double qps = (elapsedSec > 0) ? (total / elapsedSec) : 0;
        LOG_INFO("[PERF] ========== Performance Report ==========");
        LOG_INFO("[PERF] QPS: Total=%lu, Elapsed=%.1fs, QPS=%.1f req/s", total, elapsedSec, qps);
    }
    TaskMgr.GetScheduleStats().printStats("Scheduling Latency");
    TaskMgr.GetTaskStats().printStats("Task Completion");
    WorldMgr.GetPlanStats().printStats("Path Planning Latency");
    LOG_INFO("[PERF] ==========================================");

    tcpServer_->stop();  // 先切断流量入口
    workerPool_->stop(); // 等待现有任务处理完
    LOG_INFO("AgvServer Stopped.");
}

void AgvServer::OnTcpMessage(const spConnection& conn, myreactor::Buffer* buf){
    // 第一个请求到达时开始计时，排除预热期干扰
    if (!qpsStarted_.exchange(true, std::memory_order_relaxed)) {
        qpsStartTime_ = myreactor::Timestamp::now();
        LOG_INFO("[PERF] QPS timing started (first request received).");
    }
    totalRequests_.fetch_add(1, std::memory_order_relaxed);
    disPatcher_.dispatch(conn, buf);
}

}