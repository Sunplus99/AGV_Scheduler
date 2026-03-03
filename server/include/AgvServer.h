#pragma once
#include <myreactor/TcpServer.h>
#include <myreactor/Connection.h>
#include <myreactor/ThreadPool.h>
#include <myreactor/Timestamp.h>
#include <protocol/AgvCodec.h>
#include <memory>
#include <string>
#include <atomic>
#include "config/ServerConfig.h"

namespace myreactor{
    class Buffer;
}

namespace agv{

class AgvServer{
public:
    using spConnection = std::shared_ptr<myreactor::Connection>;
    
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

    std::unique_ptr<myreactor::ThreadPool> workerPool_;

    // 协议分发器
    codec::Dispatcher disPatcher_;

    // QPS 统计
    std::atomic<uint64_t> totalRequests_{0};  // 总请求计数
    myreactor::Timestamp  qpsStartTime_;      // 计时开始时间（第一个请求到达时记录）
    std::atomic<bool>     qpsStarted_{false}; // 是否已开始计时

};


}