#pragma once

#include "protocol/AgvCodec.h"
#include "model/AgvStructs.h"
#include "myreactor/Connection.h"
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>



/*
AgvSession 是 “单个 TCP 连接 + 单个 AGV 业务逻辑” 的封装体 : 每个 AGV 客户端的 TCP 连接，对应一个 AgvSession 实例。
核心使命是：把底层网络连接（Connection）和上层 AGV 业务逻辑绑定(借用Connection)，处理该 AGV 的所有消息收发和业务处理
*/
namespace myreactor{
    class ThreadPool;
}

namespace agv{
namespace session{

class AgvSession : public std::enable_shared_from_this<AgvSession> {
public:
    using spConnection = std::shared_ptr<myreactor::Connection>;

    using RpcCallback = std::function<void(bool success, const std::string& reason)>;                                     // 【RPC 增强接口】

    AgvSession(spConnection conn, myreactor::ThreadPool& pool);
    ~AgvSession();

    // ============================ 外部业务接口  ============================
    // ---------------------------------------------------------
    // “请求 - 响应”模式 (Request - Response)
    // ---------------------------------------------------------
    // 下行消息（Response）是包含在上行处理函数（Handle）内部
    // 业务处理接口 : 由 Dispatcher回调 

    void HandleLogin(const model::LoginRequest& req, int32_t seq); // AGV主动
    void HandleHbeat(const model::Heartbeat& msg, int32_t seq);    // AGV主动
    void HandleTRepo(const model::TaskReport& msg, int32_t seq);   // AGV被动
    void HandlePRequ(const model::PathRequest& req, int32_t seq);  // AGV主动

    // ---------------------------------------------------------
    // “服务器主动推送”模式 (Server Push) + RPC 支持
    // ---------------------------------------------------------
    // Server 主动发起，主动下发                    【RPC 增强接口】
    // 下行指令接口，供 TaskManager 调用 ：外部只需传入纯粹的业务结构体，无需关心 Seq 和网络细节
    bool DispatchTask(const model::TaskRequest& req, RpcCallback cb = nullptr);

    // ---------------------------------------------------------
    // 状态查询与控制
    // ---------------------------------------------------------
    // 状态查询
    int GetId() const {return agvId_;}
    bool IsLogin() const {return isLogin_;}
    // 强制下线
    void ForceClose();

    // 超时检查 : AgvManager调用，并一起绑定给TcpServer          【RPC 增强接口】
    void CheckRpcTimeout(int64_t timeoutMils = 5000);  // 5000ms = 5s

private:
    // 分装 weak.lock() 与 Codec的一体化接口 send,避免每处都要做 weak.lock()的操作
    template<typename T>
    void Send(protocol::MsgType type, const T& obj, int32_t seq = 0) {
            spConnection conn = conn_.lock();
            if (conn) 
                codec::send(conn, type, obj, seq);
    }

    // 生成下一个序列号
    inline int32_t GetNextSeq() {
        // return nextSeq.fetch_add(1) + 1;
        return ++nextSeq;
    }

    // 处理 RPC 确认, 在 TaskReport 内部调用，TaskReport也含 Ack 的作用
    void HandleAck(int32_t replyseq);               // 【RPC 增强接口】

private:
    std::weak_ptr<myreactor::Connection> conn_;

    int agvId_ = -1;
    bool isLogin_ = false;

    std::atomic<int32_t> nextSeq{0};

    struct PendingRequest {                      // 【RPC 增强接口】
        int32_t seq;
        int64_t sendTime;
        RpcCallback cb; 
    };

    std::map<int32_t, PendingRequest> pendingReqs_;   // 【RPC 增强接口】

    std::mutex mapMutex_;                               // 【RPC 增强接口】


    myreactor::ThreadPool& workerPool_;

};

}
}