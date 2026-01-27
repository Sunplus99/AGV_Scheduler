#pragma once

#include "utils/Logger.h"
#include "protocol/AgvMessage.h"
#include "model/AgvStructs.h"
#include "myreactor/Buffer.h"
#include "myreactor/Connection.h"
#include <string>
#include <cstring>
#include <functional>
#include <exception>
#include <memory>

/*
codec 是 coder（编码器） + decoder（解码器） 的缩写，中文译作「编解码器」—— 它是通信系统中「数据格式转换」的核心模块，职责是在「业务友好的格式」和「网络传输友好的格式」之间双向转换。
*/
/*
作为 AGV 通信的「格式转换中枢」，串联「业务结构体 → JSON → AgvMessage → 网络字节流」的全链路编码，以及「网络字节流 → AgvMessage → JSON → 业务结构体」的全链路解码，上层业务无需关心任何格式转换细节。
*/
/*
【JSON 序列化层】: 业务结构体 ↔ AgvMessage（中间信封）
    packMessage/unpackMessage
【网络编码层】: AgvMessage ↔ Buffer（网络字节流，处理字节序 / 粘包）
    encodeToBuffer/parseFromBuffer
【统一收发层】：整合前两层，提供「一站式」编码 + 发送接口
    send/onMessage
*/

/*
模板桥接 = 固定的抽象流程（比如 “结构体→JSON→Buffer→发送”） + 模板泛型（适配任意业务结构体），把 “不变的流程” 和 “可变的类型” 桥接起来。
    桥接模式的本质：分离「抽象层」（固定的核心流程）和「实现层」（可变的具体逻辑），让两者独立变化，避免类爆炸（比如为每个业务结构体写一个编解码类）；
    模板的作用：用「编译期多态（模板）」替代桥接模式中「运行期多态（继承 / 接口）」，让抽象层能适配任意类型的实现层，无需写重复代码。
*/
/*
抽象层：固定的编解码流程（不变的核心逻辑）
    编码流程：业务结构体 → JSON 字符串 → AgvMessage 信封 → Buffer（网络字节序） → 发送；
    解码流程：Buffer → AgvMessage 信封 → JSON 字符串 → 业务结构体。
实现层：可变的业务结构体类型（模板适配）
    不同业务场景需要不同的结构体（LoginRequest/TaskRequest/Heartbeat）,没有为每个结构体写单独的,函数，而是用模板 typename T 封装
*/


namespace agv{
namespace codec{

using spConnection = std::shared_ptr<myreactor::Connection>;

// ==========================================
// Part 1: JSON 序列化层 (Struct <-> Message)
// ==========================================

// 将 C++ 结构体转为 JSON 字符串, 填入信封 Body
/* static , 不可取：
    场景 A（模板）：利用模板的隐式 inline
    template <typename T>
    static void func(T t) { ... } // 不推荐 static，会导致代码冗余
    void func(T t) { ... }        // 推荐，模板默认 inline，链接器会自动去重
    写法 B（非模板）：加inline
    // 假如这不是模板
    static void func() { ... } // 可行，但每个 .cpp 都有副本
    inline void func() { ... } // 推荐，所有 .cpp 共享一份代码
*/
/* “template 函数在头文件中不需要加 static? 不加
    因为模板默认带有 inline 属性，链接器会处理 ODR（单一定义规则），将不同编译单元产生的相同模板实例合并，保证最终程序中只有一份代码。
    如果强行加 static，反而会赋予其内部链接属性，导致每个包含该头文件的编译单元都生成一份独立的函数副本，造成代码膨胀。
    static 在头文件中主要用于修饰全局变量或非 inline 的普通函数，以避免重复定义错误，但在现代 C++ 中，对于函数推荐用 inline，对于变量推荐用 inline constexpr (C++17)。”
*/
template <typename T>
void packMessage(protocol::AgvMessage& msg, protocol::MsgType type, const T& obj, int32_t seq = 0) {
    json j = obj;
    msg.setRawData(type, j.dump(), seq);
}

template <typename T>
T unpackMessage(const protocol::AgvMessage& msg) {
    // 防御性检查
    if(msg.head.len < 0 || static_cast<size_t>(msg.head.len) > protocol::MAX_MSG_BODY_LEN) {
        throw std::runtime_error("Invalid msg len: " + std::to_string(msg.head.len));
    }

    // 构造 string
    std::string jsonStr(msg.body, msg.head.len);

    /*
    json::exception 是 nlohmann/json 库为所有 JSON 操作异常定义的基类
    用「基类引用」捕获，能一网打尽该库抛出的所有 JSON 派生异常（比如解析错误、类型不匹配、键缺失等），是 C++ 异常处理 “多态捕获” 的典型用法。
    */
    try{
        // 解析 JSON
        /*
        j.get_to(T& obj):输出参数方式写入解析结果,无返回值，直接修改传入的引用对象，语义是 “解析并写入”
        j.get<T>()：返回值方式获取解析结果，返回值为 T 类型对象，会创建新对象并返回，语义是 “获取并返回”
        */
        json j = json::parse(jsonStr);
        return j.get<T>();
    } catch (const json::exception& e) {
        /* 捕获了底层库的错误，加上了当前的上下文信息（ MsgType），再抛出一个通用的 runtime_error*/
        // 捕获底层库的异常 -> 包装成带有业务信息的标准异常 -> 抛出
        throw std::runtime_error("JSON Parse Error [Type=" + protocol::msgTypeToString(static_cast<protocol::MsgType>(msg.head.type)) + "]: " + e.what());
    }
}


// ==========================================
// Part 2: 网络编码层 (Message -> Buffer)
// ==========================================

// 将信封里的 Head 和 Body 写入 Buffer 对象，准备发送
// 使用 Buffer 作为序列化工具
inline void encodeToBuffer(const protocol::AgvMessage& msg, myreactor::Buffer& buf) {
    // 包头
    buf.appendInt32(msg.head.len);
    buf.appendInt32(msg.head.type);
    buf.appendInt32(msg.head.seq);

    // 包体
    buf.append(msg.body, msg.head.len);
}

// 定义解析结果状态
enum class ParseResult {
    kSuccess,       // 成功切出一个包
    kNotEnough,     // 半包，继续等
    kError          // 致命错误，协议非法
};  // k : Constant（常量）

inline ParseResult parseFromBuffer(myreactor::Buffer* buf, protocol::AgvMessage& outMsg) {
    // 1.检查包头长度 ：至少有包头
    if(buf->size() < sizeof(protocol::MsgHead))
        return ParseResult::kNotEnough;

    // 2.获取包长但不动数据
    int32_t len = buf->peekInt32();

    // 2.1安全检查 (防恶意大包)
    /*防御性关闭
    在工业级服务器（如 Nginx, Redis, Muduo）中，遇到 协议解析错误（Protocol Error），标准处理流程只有一个：立马踢掉客户端（Close Connection）
    这是因为：
        Bug：客户端代码可能有严重 Bug，发了乱码，留着它也没用。
        攻击：这可能是恶意攻击（尝试缓冲区溢出或 DoS），必须立刻切断。
        无法恢复：TCP 只有字节流，没有“标记点”，一旦乱序，无法重新同步。
    */
    if(len < 0 || len > protocol::MAX_MSG_BODY_LEN-1) {
        // 严重错误 : 丢弃
        LOG_ERROR("Protocol Error: Invalid msg len=%d, closing connection.", len);
        buf->clear();
        return ParseResult::kError;
    }

    // 2.2检查数据够不够一个完整的包，不够等下次 
    if(buf->size() < sizeof(protocol::MsgHead)+len)
        return ParseResult::kNotEnough;

    // 3.正式读取
    outMsg.head.len  = buf->readInt32();
    outMsg.head.type = buf->readInt32();
    outMsg.head.seq  = buf->readInt32();
    
    if(len > 0) {
        ::memcpy(outMsg.body, buf->data(), len);
        // 封口
        outMsg.body[len] = '\0';
        // 读完即删
        buf->erase(len);
    } else { // len == 0
        outMsg.body[0] = '\0';
    }

    return ParseResult::kSuccess;
}


// ==========================================
// Part 3: 一体化发送接口 send 
// ==========================================

/*
发送端和接收端的「通信场景本质不同」：
    send 是「主动、可控、单发」的，一个函数就能满足所有需求；而
    onMessage 是「被动、不可控、需处理粘包 / 半包」的，需要两个重载函数来覆盖「单条解析」和「批量解析」两种核心场景，且这两个函数是互补而非重复。
*/
/*
发送侧核心：临时 Buffer 是「序列化工具」，拼接数据 + 转字节序后传给 conn->send，发不完的进 outputBuffer_；
接收侧核心：直接操作 Connection::inputBuffer_（Buffer* buf），无需额外 Buffer，利用 peekInt32/readInt32/retrieve 处理粘包 / 拆包
*/
template <typename T>
static void send(const spConnection& conn,
                 protocol::MsgType type, 
                 const T& obj, 
                 int32_t seq = 0)
{
    if(!conn) return;

    /*从对象池申请
      这是一个大对象 (64KB), 防爆栈 （不在栈上）
      使用 new 会触发 AgvMessage::operator new，从 ObjectPool 极速分配
    */ 
    /*
    由工作线程计算出路径，得到消息结构体 T，然后调用 codec::send，此处，调用时处在 bussinessLogic 里。 
    所以如果开启了工作线程，这个对象池是在工作线程里的，如果没开启，就在IO线程里。
    多线程下：thread_local 保证了不同线程用不同的池子，物理隔离，不需要锁。
    单线程下：代码是串行执行的，同一个线程先后访问同一个池子，逻辑隔离，不需要锁。  
    */
    auto* msg = new protocol::AgvMessage();

    packMessage(*msg, type, obj, seq); // *msg解引用：根据地址找到对象

    // 这里定义栈变量 Buffer 是安全的，因为它初始很小，会自动扩容到堆上 : buf.buffer_ (vector)会自动扩容到堆上而buf对象本身小(非静态成员+内存对齐+虚函数指针 (vptr))
    myreactor::Buffer buf;
    encodeToBuffer(*msg, buf);  // *msg解引用：根据地址找到对象

    // 底层发送
    conn->send(&buf);

    // 归还对象池 ： 触发 AgvMessage::operator delete
    delete msg;

}


// ==========================================
// Part 4: 接收分发器 Dispatcher
// ==========================================

// 逻辑死锁：不能像send一样直接用模板函数一体化，因为并不知道接收的是什么类型

/*
开场白（定义角色）： “Dispatcher 类是我服务器中的通信中枢，它位于底层网络库（Reactor）和上层业务逻辑之间，起到了承上启下的桥梁作用。”

第一点：为什么要设计它？（解决痛点 / 架构思想） “核心目的是为了彻底解耦。 传统的网络编程往往需要在底层写一个巨大的 switch-case 来根据消息类型强转结构体，这违背了开闭原则，每次加业务都要改底层。 而我采用了注册驱动的模式，让业务层只管注册回调，不用关心底层通信，实现了网络层和业务层的分离。”

第二点：是怎么实现的？（展示 C++ 功底 —— 核心得分点） “在技术实现上，我利用了 C++ 的 模板 (Templates) 和 Lambda 闭包。 我设计了一个泛型的 registerHandler 接口。它最大的亮点是自动化反序列化：当业务层注册一个处理函数（比如处理 LoginRequest）时，我在内部自动生成了一个适配层，负责把通用的二进制消息 (AgvMessage) 转成具体的业务结构体。 这样一来，业务代码里拿到的直接就是强类型的对象，非常干净。”

第三点：运行时怎么工作？（展示网络编程经验） “在运行时，它维护了一个消息类型到回调函数的映射表（std::map）。 当网络数据到达时，Dispatcher 会先负责处理 TCP 的粘包和半包问题，切分出完整的信封；然后利用对象池快速申请内存；最后查表分发给具体的业务逻辑。 同时，我也做了异常隔离，在分发层捕获所有 JSON 解析异常，确保单个消息的错误不会导致整个服务器崩溃。”

总结（一句话收尾）： “总的来说，这个类封装了粘包处理、反序列化自动化以及路由分发，保证了 AGV 服务器通信的高效和健壮。”

问：为什么用 std::map？性能够吗？
    答：消息类型通常只有几十种，std::map (红黑树) 的查找开销微乎其微。如果追求极致，我可以换成 std::unordered_map (哈希表) 甚至直接用 std::vector (如果 MsgType 是连续整数) 来实现 O(1) 查找。
问：如果不加 try-catch 会怎样？
    答：因为 unpackMessage 可能会抛出 JSON 解析异常，或者用户回调里可能有逻辑错误。如果不捕获，异常会穿透到 EventLoop 导致整个 IO 线程崩溃，服务器就挂了。所以这里必须兜底。
问：为什么设计成类？
    它必须设计成类而非函数，因为需要维护一张 “消息类型到业务回调” 的映射表，函数没法持久化管理这种有状态的内容。
*/

/*
有关Dispatcher两个函数的签名：dispatch的调用场景应该是最底层channel::handleevent内的readcallback，回调Connection::handleread,其中有readcb_，即回调TcpServer::readconntion,中有onmess_，即回调dispatch，中有callbacks[x]->second,所以现在可以确定dispatch的签名和Connection::handleread与Connection::handleread一致，都为const spConnection& conn, Buffer* buf,以及callbacks[x]->second的函数签名，即 callbacks_[type] = [userFunc](const TcpConnectionPtr& conn, const protocol::AgvMessage& msg)的函数签名也是和const spConnection& conn, Buffer* buf强相关。
*/

class Dispatcher {
public:
    using MessageCB = std::function<void(const spConnection&, const protocol::AgvMessage&)>;

    // 注册业务回调
    /*
    AgvServer::Dispatcher_调用它时，是传入 MsgType 和 handler 来绑定，并且存在 Dispatcher::map中 
    
    T 只出现在函数体内部（unpackMessage<T>/T obj），不是函数参数的直接类型，编译器无法从传入的实参中推导T的类型 ， 调用函数时必须显示指定 T （<T>）
    UserFunc是函数参数userFunc的类型（UserFunc&& userFunc），编译器可以通过传入的实参（比如handleLogin函数、lambda）直接推导其类型
    
    在服务器开发中，“业务处理”通常 = “计算 + 响应”以及序列号:
        【计算】必须要源数据，即obj, 而obj在不同时期表现为msg和obj，
        【响应】依靠 conn, conn的本质: “回信地址”。
        【序列号】即 msg.head.seq
    因此 UserFunc 的 参数列表必需的就是以上 3 个 （早期表现为 msg + conn 两个）
    */
    template<typename T, typename UserFunc>
    void registerHandler(protocol::MsgType type, UserFunc&& userFunc){
        callbacks_[type] = [userFunc]
        (const spConnection& conn, const protocol::AgvMessage& msg) 
        {
            try{
                // 获取 内容结构
                T obj = unpackMessage<T>(msg);
                // 调用业务逻辑
                userFunc(conn, obj, msg.head.seq);
            } catch (const std::exception& e){
                /*
                ① 捕获 unpackMessage 抛出的异常： 在 unpackMessage 中，无论是长度非法抛出的 runtime_error，还是 JSON 解析失败后 catch 住并重新抛出（Rethrow） 的 runtime_error，它们的本质都是 std::runtime_error 对象;std::exception e 是基类对象,根据 C++ 的多态性，基类的引用可以捕获派生类的对象
                ② 捕获 userFunc 业务代码） 中可能发生的未知错误
                */
                LOG_ERROR("Handle error: %s",e.what());
            }
        };

    }


    // 核心分发逻辑(接收与分发一体化)
    void dispatch(const spConnection& conn, myreactor::Buffer* buf) {
        while(true) {
            // auto* msg = new protocol::AgvMessage();
            auto msg = std::make_unique<protocol::AgvMessage>();

            ParseResult result = parseFromBuffer(buf, *msg);

            if (result == ParseResult::kError) {
                // delete msg;  // 有new必须有delete
                conn->forceClose();
                break;
            }

            if (result == ParseResult::kNotEnough) {
                // delete msg;  // 有new必须有delete
                break;    // 半包, 下次再说
            }

            // 查找并执行
            // map.find(key) , 写成这样方便对 type 做检查, 如果直接callbacks_[xxx], 找不到会添加 该键的 pair
            /*在业务末端分流
            保持 Dispatcher 运行在 IO 线程不变。让 AgvSession 自己根据业务的轻重缓急来决定：简单任务直接在IO线程解决，计算密集型任务扔给工作线程：
                1. 修改前的流程 (全在 IO 线程)
                    Connection::Read -> Dispatcher::dispatch -> AgvSession::HandlePRequ (A* 计算卡死 IO) -> Send
                2. 修改后的流程 (按需分流)
                    Connection::Read -> Dispatcher::dispatch -> AgvSession::HandlePRequ (投递任务并立即返回) -> IO 线程解放
                只有 HandlePRequ 这一步发生了变化。
            */
            auto it = callbacks_.find(static_cast<protocol::MsgType>(msg->head.type));
            if(it == callbacks_.end()) {
                LOG_WARN("Unknow msg type: %d", msg->head.type);
            } else {
                it->second(conn, *msg);
            }

            // delete msg;
        }
    }

private:
    // 消息类型到业务回调 的 映射表
    std::map<protocol::MsgType, MessageCB> callbacks_;
};



}
}