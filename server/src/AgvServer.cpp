#include "AgvServer.h"
#include "session/AgvManager.h"
#include "manager/TaskManager.h"
#include "manager/WorldManager.h"
#include "utils/Logger.h"



namespace agv{

using namespace model;
using namespace protocol;

/*AgvServer::AgvServer(const std::string& ip, const uint16_t& port, 
                     int iothreadnum, int workthreadnum,
                     int tcpTimeoutSec, int rpcTimeoutMs,
                     const std::string& mapPath
                    ) 
    : tcpServer_(std::make_unique<myreactor::TcpServer>(ip, port, iothreadnum)),
      workerPool_(std::make_unique<myreactor::ThreadPool>(workthreadnum)),
      disPatcher_(),
      tcpTimeoutSec_(tcpTimeoutSec),
      rpcTimeoutMs_(rpcTimeoutMs),
      mapPath_(mapPath)*/
AgvServer::AgvServer(const config::ServerConfig& config)
    : config_(config),
      tcpServer_(std::make_unique<myreactor::TcpServer>(config.ip, config.port, config.ioThreadnum)),
      workerPool_(std::make_unique<myreactor::ThreadPool>(config.workerThreadnum)),
      disPatcher_()
{
    /*
    最佳实践 (RAII 原则)： 对象一旦构造完成，就应该是“完全可用”的状态。
    将构造函数变成一个干净的“总指挥”，但将具体的脏活累活拆分成几个私有的初始化函数
    */
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
    /* bind 写法
    tcpServer_->setnewconncb(std::bind(&session::AgvManager::OnNewConn, &AgvMgr, std::placeholders::_1));
    */
    tcpServer_->setnewconncb( [this](const spConnection& conn){
        AgvMgr.OnNewConn(conn,*(this->workerPool_));  // 解引用，传引用，表示该参数不能为空，是 Session 存活的必要条件。 *(this->workerPool_) 配合 ThreadPool&
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
    /*成员变量不能直接出现在 lambda 捕获列表中
    lambda 的捕获列表（[]里的内容）只能捕获 “当前作用域的局部变量 / 函数参数”，无法直接捕获 “类的成员变量”—— 因为成员变量属于 “对象实例”，而非 “当前函数作用域”。
    */
    tcpServer_->setTickcb( [this](){ 
        AgvMgr.CheckAllTimeouts(this->config_.rpcTimeoutMs);
    });

    // tcpServer_->seterrorcb();    暂时不需要
    // tcpServer_->setsendcompcb();  不需要
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
            // if(auto sess = AgvMgr.GetSession(conn))
            if(auto sess = conn->getContext<session::AgvSession>())
                sess->HandleHbeat(hb, seq);
        }
    );

    disPatcher_.registerHandler<TaskReport>(
        MsgType::TASK_REPORT,
        [](const spConnection& conn, const TaskReport& msg, int32_t seq){
            // if(auto sess = AgvMgr.GetSession(conn))
            if(auto sess = conn->getContext<session::AgvSession>())
                sess->HandleTRepo(msg, seq);
        }
    );

    disPatcher_.registerHandler<PathRequest>(
        MsgType::PATH_REQ,
        [](const spConnection& conn, const PathRequest& req, int32_t seq){
            // if(auto sess = AgvMgr.GetSession(conn))
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
    tcpServer_->stop();  // 先切断流量入口
    workerPool_->stop(); // 等待现有任务处理完
    LOG_INFO("AgvServer Stopped.");
}

/*单独处理为成员函数：
调试断点方便：
    如果想调试“所有收到的 TCP 消息”，只需要在 AgvServer::OnTcpMessage 函数入口打一个断点。
    如果是 Lambda，断点有时候不太好打（特别是代码挤在一起时），或者栈信息显示不如具名函数清晰。
统一拦截点 (Interceptor/Hook)：
    AgvServer::OnTcpMessage 代表了 “消息正式进入应用层” 的大门。
    未来如果想加 全局流量统计（每秒收了多少字节）、全局日志（收到包了，打印个 Debug）、或者 IP 黑名单过滤（在交给 Dispatcher 解析前先拦截），这里是绝佳的位置。
*/
void AgvServer::OnTcpMessage(const spConnection& conn, myreactor::Buffer* buf){
    disPatcher_.dispatch(conn, buf);
}

}