#pragma once
#include <myreactor/TcpServer.h>
#include <myreactor/Connection.h>
#include <myreactor/ThreadPool.h>
#include <protocol/AgvCodec.h>
#include <memory>
#include <string>
#include "config/ServerConfig.h"

namespace myreactor{
    class Buffer;
}

namespace agv{

class AgvServer{
public:
    using spConnection = std::shared_ptr<myreactor::Connection>;

    // AgvServer(const std::string& ip, const uint16_t& port, 
    //           int iothreadnum=2, int workthreadnum=2,
    //           int tcpTimeiurSec = 60,   // TCP 心跳超时
    //           int rpcTimeoutMs = 5000,  // RPC 业务超时
    //           const std::string& mapPath = "" // 地图路径
    //          );
    
    // 单参数构造函数 explicit防隐式转换
    explicit AgvServer(const config::ServerConfig& config);

    ~AgvServer();

    void Start();
    void Stop();

private:
    // 总初始化入口
    void Init();

    // 1st. 基础设置 (依赖注入)
    void SetupInfra();

    // 2nd. 系统资源 (地图加载、未来数据库连接等)
    void InitSysRes();

    // 3rd. 底层回调
    void SetupNecbs();

    // 4th. 业务层注册
    void RegisBnhad();
    
    // 底层 TCP 消息处理回调
    void OnTcpMessage(const spConnection& conn, myreactor::Buffer* buf);

private:
    config::ServerConfig config_;

    std::unique_ptr<myreactor::TcpServer> tcpServer_;

    /*
    资源归属权在上层，使用权下放给下层
    AgvSession 与 TaskManager 不再依赖那个巨大的 AgvServer 类，它只依赖它真正需要的 ThreadPool
    */
    std::unique_ptr<myreactor::ThreadPool> workerPool_;

    // 协议分发器
    codec::Dispatcher disPatcher_;

    // 配置参数存储
    // int tcpTimeoutSec_;
    // int rpcTimeoutMs_;
    // std::string mapPath_;

};


}