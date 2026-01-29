# AGV 智能调度系统 - 详细技术报告

> **面试准备文档**：全面讲解项目架构、技术要点和面试问答

---

## 目录

1. [项目概述](#1-项目概述)
2. [系统架构设计](#2-系统架构设计)
3. [核心技术详解](#3-核心技术详解)
4. [C++ 技术要点](#4-c-技术要点)
5. [网络编程要点](#5-网络编程要点)
6. [并发编程要点](#6-并发编程要点)
7. [算法设计要点](#7-算法设计要点)
8. [面试常见问题](#8-面试常见问题)
9. [项目亮点总结](#9-项目亮点总结)

---

## 1. 项目概述

### 1.1 项目背景

**业务场景**：仓储物流中心的 AGV 自动化调度

- **问题**：多辆 AGV 需要协同工作，完成货物搬运任务
- **挑战**：
  - 如何高效分配任务？（调度问题）
  - 如何避免碰撞？（路径规划问题）
  - 如何处理高并发？（网络编程问题）

**技术选型**：

- **语言**：C++17（高性能、底层控制）
- **网络模型**：Reactor（事件驱动、高并发）
- **并发模型**：线程池（充分利用多核）
- **通信协议**：自定义二进制协议（高效、紧凑）

### 1.2 系统功能

| 功能模块 | 功能描述 |
|---------|---------|
| **连接管理** | AGV 登录、心跳保活、断线重连 |
| **任务调度** | 动态任务生成、智能分配、负载均衡 |
| **路径规划** | A* 算法、实时避障、路径重规划 |
| **状态管理** | AGV 位置、任务进度、地图状态 |
| **日志监控** | 异步日志、性能监控、故障诊断 |

### 1.3 技术指标

- **并发能力**：支持 100+ AGV 同时在线
- **响应延迟**：任务分配 <5ms，路径规划 <10ms
- **吞吐量**：1000+ 任务/分钟
- **可用性**：7x24 小时稳定运行

---

## 2. 系统架构设计

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                     AGV 调度系统架构                          │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                   应用层 (Application)                │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │   │
│  │  │ TaskManager  │  │WorldManager  │  │ AgvManager│  │   │
│  │  │  (任务调度)   │  │ (地图/状态)  │  │ (会话管理)│  │   │
│  │  └──────────────┘  └──────────────┘  └───────────┘  │   │
│  └──────────────────────────────────────────────────────┘   │
│                           ▲                                   │
│                           │                                   │
│  ┌────────────────────────┴──────────────────────────────┐  │
│  │                   业务层 (Business)                    │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │  │
│  │  │   Scheduler  │  │    Planner   │  │  Protocol │  │  │
│  │  │  (调度算法)   │  │  (路径规划)  │  │  (协议)   │  │  │
│  │  └──────────────┘  └──────────────┘  └───────────┘  │  │
│  └──────────────────────────────────────────────────────┘  │
│                           ▲                                   │
│                           │                                   │
│  ┌────────────────────────┴──────────────────────────────┐  │
│  │                   网络层 (Network)                     │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │  │
│  │  │  TcpServer   │  │  Connection  │  │  Codec    │  │  │
│  │  │  (监听器)     │  │  (连接)      │  │  (编解码) │  │  │
│  │  └──────────────┘  └──────────────┘  └───────────┘  │  │
│  └──────────────────────────────────────────────────────┘  │
│                           ▲                                   │
│                           │                                   │
│  ┌────────────────────────┴──────────────────────────────┐  │
│  │                   传输层 (Transport)                   │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌───────────┐  │  │
│  │  │  EventLoop   │  │  ThreadPool  │  │   Epoll   │  │  │
│  │  │  (事件循环)   │  │  (线程池)    │  │  (IO复用) │  │  │
│  │  └──────────────┘  └──────────────┘  └───────────┘  │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

### 2.2 分层设计

#### 传输层（Transport Layer）

**职责**：底层网络通信

- **EventLoop**：事件循环，基于 Epoll 的 IO 多路复用
- **ThreadPool**：线程池，分离 IO 和计算
- **Epoll**：Linux 高性能 IO 复用机制

**设计要点**：
- 主从 Reactor 模式：主 Reactor 负责 Accept，从 Reactor 负责 IO
- 非阻塞 IO：ET 模式，减少系统调用
- One Loop Per Thread：每个线程一个事件循环

#### 网络层（Network Layer）

**职责**：连接管理和消息收发

- **TcpServer**：TCP 服务器，管理监听和连接
- **Connection**：TCP 连接，封装 Socket 操作
- **Codec**：编解码器，处理粘包和协议解析

**设计要点**：
- 长度前缀协议：`[Length|Type|Seq|Body]`
- 零拷贝：环形缓冲区，减少内存拷贝
- 超时管理：心跳保活 + TCP 超时

#### 业务层（Business Layer）

**职责**：核心业务逻辑

- **Scheduler**：调度算法（贪心、遗传等）
- **Planner**：路径规划（A*、Dijkstra 等）
- **Protocol**：业务协议（登录、任务、心跳等）

**设计要点**：
- 策略模式：算法可插拔
- 工厂模式：动态创建算法实例
- 模板方法：统一算法接口

#### 应用层（Application Layer）

**职责**：全局管理和协调

- **TaskManager**：任务管理（创建、分配、跟踪）
- **WorldManager**：世界管理（地图、AGV 状态）
- **AgvManager**：会话管理（连接、断线）

**设计要点**：
- 单例模式：全局唯一实例
- 观察者模式：事件驱动更新
- 读写锁：多读单写优化

### 2.3 数据流

#### 任务下发流程

```
1. WMS 线程创建任务
   └─> TaskManager::AddTask()
       └─> 任务入队 pendingTasks_
           └─> TryDispatch() 触发调度
               └─> 投递到 Worker 线程池
                   └─> ExecuteDispatch()
                       ├─> 获取 AGV 快照
                       ├─> 调用调度算法
                       ├─> 获取决策结果
                       └─> 遍历决策
                           ├─> Double Check 状态
                           ├─> 获取 Session
                           ├─> DispatchTask() 下发
                           │   └─> 投递到 IO 线程
                           │       └─> Send() 发送
                           └─> 更新 runningTasks_
```

#### 路径规划流程

```
1. AGV 收到任务
   └─> 发送 PATH_REQ
       └─> Server 收到请求
           └─> HandlePRequ()
               └─> 投递到 Worker 线程池
                   └─> WorldManager::PlanPath()
                       ├─> 获取地图
                       ├─> 调用 A* 算法
                       ├─> 返回路径
                       └─> Send PATH_RESP
                           └─> AGV 收到路径
                               └─> 开始移动
```

---

## 3. 核心技术详解

### 3.1 Reactor 网络模型

#### 什么是 Reactor？

**定义**：事件驱动的网络编程模型

**核心思想**：
- **事件驱动**：基于事件（可读、可写、错误）触发回调
- **IO 多路复用**：一个线程监听多个连接
- **非阻塞 IO**：不阻塞等待，提高并发

#### 主从 Reactor 模式

```
┌─────────────────────────────────────────┐
│           Main Reactor (主)              │
│  ┌─────────────────────────────────┐   │
│  │  Acceptor (监听新连接)           │   │
│  │  ┌──────┐  ┌──────┐  ┌──────┐  │   │
│  │  │ Conn1│  │ Conn2│  │ Conn3│  │   │
│  │  └───┬──┘  └───┬──┘  └───┬──┘  │   │
│  └──────┼─────────┼─────────┼──────┘   │
└─────────┼─────────┼─────────┼──────────┘
          │         │         │
          ▼         ▼         ▼
┌─────────────────────────────────────────┐
│        Sub Reactors (从，多个)           │
│  ┌──────────┐  ┌──────────┐  ┌────────┐│
│  │ IO Loop1 │  │ IO Loop2 │  │IO Loop3││
│  │  (线程1) │  │  (线程2) │  │(线程3) ││
│  └──────────┘  └──────────┘  └────────┘│
└─────────────────────────────────────────┘
```

**优势**：
- 主 Reactor 专注 Accept，从 Reactor 专注 IO
- 充分利用多核 CPU
- 负载均衡：连接分散到多个线程

#### 本项目实现

```cpp
// TcpServer 启动
void TcpServer::start() {
    // 1. 主 Reactor：监听新连接
    acceptor_->listen();
    
    // 2. 从 Reactor：IO 线程池
    for (int i = 0; i < ioThreadNum_; ++i) {
        ioThreadPool_->start();  // 每个线程一个 EventLoop
    }
    
    // 3. Worker 线程池：计算密集任务
    workerPool_->start();
}

// 新连接到来
void TcpServer::onNewConnection(int sockfd) {
    // Round-Robin 分配到 IO 线程
    EventLoop* ioLoop = ioThreadPool_->getNextLoop();
    
    // 创建 Connection
    auto conn = std::make_shared<Connection>(ioLoop, sockfd);
    
    // 注册到 Epoll
    ioLoop->addChannel(conn->channel());
}
```

**面试要点**：
- 为什么用主从 Reactor？（分离 Accept 和 IO，提高并发）
- 为什么用线程池？（避免频繁创建销毁线程）
- 如何负载均衡？（Round-Robin 轮询分配）

### 3.2 线程池设计

#### 为什么需要线程池？

**问题**：
- 频繁创建销毁线程开销大
- 线程数量不可控，可能耗尽资源
- 难以管理和监控

**解决**：
- 预先创建固定数量的线程
- 任务队列 + 工作线程
- 生产者-消费者模式

#### 本项目实现

```cpp
class ThreadPool {
public:
    ThreadPool(int numThreads) {
        for (int i = 0; i < numThreads; ++i) {
            threads_.emplace_back([this]() {
                while (running_) {
                    Task task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        // 等待任务
                        cond_.wait(lock, [this]() {
                            return !running_ || !tasks_.empty();
                        });
                        
                        if (!running_ && tasks_.empty()) return;
                        
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();  // 执行任务
                }
            });
        }
    }
    
    void addTask(Task task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push(std::move(task));
        }
        cond_.notify_one();  // 唤醒一个线程
    }
};
```

**设计要点**：
- **任务队列**：`std::queue<Task>`
- **互斥锁**：保护任务队列
- **条件变量**：线程等待和唤醒
- **RAII**：析构时自动停止线程

**面试要点**：
- 为什么用条件变量？（避免忙等待，节省 CPU）
- 如何优雅停止？（设置标志位 + notify_all）
- 如何避免死锁？（锁的粒度、加锁顺序）

### 3.3 A* 路径规划

#### 算法原理

**核心思想**：启发式搜索

```
f(n) = g(n) + h(n)

g(n): 起点到当前点的实际代价
h(n): 当前点到终点的估计代价（曼哈顿距离）
f(n): 总代价估计
```

**优势**：
- 比 Dijkstra 快（有方向性）
- 比贪心准（考虑实际代价）
- 保证最优（启发函数满足条件）

#### 本项目实现

```cpp
std::vector<Point> AStarSolver::FindPath(
    const GridMap& map, 
    const Point& start, 
    const Point& end) 
{
    // 1. 初始化
    std::priority_queue<Node*, std::vector<Node*>, NodeCompare> openList;
    std::unordered_set<Point> closedList;
    
    Node* startNode = createNode(start);
    startNode->g = 0;
    startNode->h = CalcH(start, end);  // 曼哈顿距离
    startNode->f = startNode->g + startNode->h;
    openList.push(startNode);
    
    // 2. 搜索
    while (!openList.empty()) {
        Node* current = openList.top();
        openList.pop();
        
        // 到达终点
        if (current->pos == end) {
            return reconstructPath(current);
        }
        
        closedList.insert(current->pos);
        
        // 3. 扩展邻居
        for (const auto& dir : directions) {
            Point neighbor = current->pos + dir;
            
            // 检查合法性
            if (map.IsObstacle(neighbor)) continue;
            if (closedList.count(neighbor)) continue;
            
            // 计算代价
            int newG = current->g + 1;
            Node* neighborNode = getOrCreateNode(neighbor);
            
            if (newG < neighborNode->g) {
                neighborNode->g = newG;
                neighborNode->h = CalcH(neighbor, end);
                neighborNode->f = neighborNode->g + neighborNode->h;
                neighborNode->parent = current;
                openList.push(neighborNode);
            }
        }
    }
    
    return {};  // 无路径
}
```

**优化技巧**：
- **内存池**：预分配节点，避免频繁 new/delete
- **哈希表**：快速查找节点
- **优先队列**：高效获取最小 f 值节点

**面试要点**：
- A* 和 Dijkstra 的区别？（启发函数）
- 如何保证最优？（h(n) 不高估实际代价）
- 时间复杂度？（O(b^d)，b 是分支因子，d 是深度）


### 3.4 贪心调度算法

#### 算法思想

**目标**：最小化 AGV 移动成本

**策略**：距离优先
- 对每个任务，选择距离最近的空闲 AGV
- 贪心选择，局部最优

#### 本项目实现

```cpp
std::vector<DispatchResult> GreedyScheduler::Dispatch(
    const std::vector<TaskContext>& tasks,
    const std::vector<AgvInfo>& candidates)
{
    std::vector<DispatchResult> results;
    std::set<int> assignedAgvs;  // 防止重复分配
    
    for (const auto& task : tasks) {
        int bestAgvId = -1;
        int minDistance = INT_MAX;
        
        // 遍历所有候选 AGV
        for (const auto& agv : candidates) {
            if (assignedAgvs.count(agv.uid)) continue;
            
            // 计算曼哈顿距离
            int dist = abs(agv.pos.x - task.target.x) + 
                      abs(agv.pos.y - task.target.y);
            
            if (dist < minDistance) {
                minDistance = dist;
                bestAgvId = agv.uid;
            }
        }
        
        if (bestAgvId != -1) {
            results.push_back({task, bestAgvId, minDistance});
            assignedAgvs.insert(bestAgvId);
        }
    }
    
    return results;
}
```

**优化方向**：
- **遗传算法**：全局最优
- **匈牙利算法**：最优匹配
- **强化学习**：自适应学习

**面试要点**：
- 贪心算法的局限？（局部最优 ≠ 全局最优）
- 如何改进？（考虑未来任务、负载均衡）
- 时间复杂度？（O(n*m)，n 是任务数，m 是 AGV 数）

---

## 4. C++ 技术要点

### 4.1 智能指针

#### shared_ptr：共享所有权

**使用场景**：
- Connection 被多个地方持有（TcpServer、EventLoop、回调）
- Session 被 AgvManager 和 Connection 共享

```cpp
// 创建
auto conn = std::make_shared<Connection>(loop, sockfd);

// 共享
connections_[sockfd] = conn;  // TcpServer 持有
conn->setContext(session);    // Connection 持有 Session

// 自动释放：引用计数为 0 时自动 delete
```

**面试要点**：
- 引用计数原理？（原子操作，线程安全）
- 循环引用问题？（用 weak_ptr 打破）
- 性能开销？（两次内存分配，可用 make_shared 优化）

#### weak_ptr：弱引用

**使用场景**：
- Session 持有 Connection 的弱引用（避免循环引用）
- 回调中持有对象的弱引用（避免悬空指针）

```cpp
class AgvSession {
    std::weak_ptr<Connection> conn_;  // 弱引用
    
    void Send(const Message& msg) {
        auto conn = conn_.lock();  // 尝试提升为 shared_ptr
        if (conn) {
            conn->send(msg);
        }
    }
};
```

**面试要点**：
- 为什么需要 weak_ptr？（打破循环引用）
- lock() 的作用？（线程安全地提升为 shared_ptr）
- 如何判断对象是否存活？（expired()）

#### unique_ptr：独占所有权

**使用场景**：
- 工厂模式返回对象
- RAII 资源管理

```cpp
// 创建
auto logger = std::make_unique<AsyncLogging>(filename);

// 转移所有权
async_logger_ = std::move(logger);

// 自动释放：离开作用域自动 delete
```

**面试要点**：
- 和 shared_ptr 的区别？（独占 vs 共享）
- 如何转移所有权？（std::move）
- 性能优势？（无引用计数开销）

### 4.2 RAII（资源获取即初始化）

#### 核心思想

**定义**：资源的生命周期绑定到对象的生命周期

**优势**：
- 自动管理资源（内存、文件、锁）
- 异常安全（析构函数自动调用）
- 代码简洁（无需手动释放）

#### 本项目应用

```cpp
// 1. 锁管理
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 临界区代码
}  // 自动解锁

// 2. 文件管理
class FileGuard {
    FILE* file_;
public:
    FileGuard(const char* path) : file_(fopen(path, "w")) {}
    ~FileGuard() { if (file_) fclose(file_); }
};

// 3. 连接管理
class Connection {
    int sockfd_;
public:
    Connection(int fd) : sockfd_(fd) {}
    ~Connection() { close(sockfd_); }
};
```

**面试要点**：
- RAII 的好处？（异常安全、自动管理）
- 和 GC 的区别？（确定性析构 vs 不确定性回收）
- 如何实现？（构造获取、析构释放）

### 4.3 移动语义

#### 右值引用

**定义**：`T&&` 表示右值引用

**作用**：避免不必要的拷贝

```cpp
// 拷贝构造（昂贵）
std::vector<int> v1 = getVector();  // 拷贝

// 移动构造（高效）
std::vector<int> v2 = std::move(getVector());  // 移动
```

#### 本项目应用

```cpp
// 1. 任务队列
void ThreadPool::addTask(Task task) {
    tasks_.push(std::move(task));  // 移动，避免拷贝
}

// 2. 智能指针转移
async_logger_ = std::move(new_logger);

// 3. 容器操作
threads_.emplace_back([this]() { /* ... */ });  // 原地构造
```

**面试要点**：
- 左值和右值的区别？（能取地址 vs 不能取地址）
- std::move 的作用？（强制转换为右值引用）
- 移动构造和拷贝构造的区别？（浅拷贝 vs 深拷贝）

### 4.4 模板编程

#### 策略模式 + 模板

```cpp
// 调度器接口
class ITScheduler {
public:
    virtual std::vector<DispatchResult> Dispatch(
        const std::vector<TaskContext>& tasks,
        const std::vector<AgvInfo>& agvs) = 0;
};

// 具体策略
class GreedyScheduler : public ITScheduler { /* ... */ };
class GeneticScheduler : public ITScheduler { /* ... */ };

// 使用
std::shared_ptr<ITScheduler> scheduler_;
scheduler_ = std::make_shared<GreedyScheduler>();
auto results = scheduler_->Dispatch(tasks, agvs);  // 多态
```

**面试要点**：
- 模板和虚函数的区别？（编译期 vs 运行期）
- 何时用模板？（类型无关、性能敏感）
- 何时用虚函数？（运行时多态、策略切换）

---

## 5. 网络编程要点

### 5.1 IO 多路复用

#### Epoll 原理

**核心思想**：一个线程监听多个文件描述符

**三个系统调用**：
```cpp
// 1. 创建 epoll 实例
int epfd = epoll_create1(0);

// 2. 注册事件
struct epoll_event ev;
ev.events = EPOLLIN | EPOLLET;  // 可读 + 边缘触发
ev.data.fd = sockfd;
epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

// 3. 等待事件
struct epoll_event events[MAX_EVENTS];
int nfds = epoll_wait(epfd, events, MAX_EVENTS, timeout);
for (int i = 0; i < nfds; ++i) {
    handleEvent(events[i]);
}
```

#### LT vs ET 模式

| 模式 | 触发条件 | 优缺点 |
|------|---------|--------|
| **LT（水平触发）** | 只要有数据就触发 | 简单，但可能重复触发 |
| **ET（边缘触发）** | 状态变化时触发 | 高效，但需要一次读完 |

**本项目选择**：ET 模式（高性能）

```cpp
// ET 模式：必须一次读完
void Connection::handleRead() {
    while (true) {
        ssize_t n = read(sockfd_, buf, sizeof(buf));
        if (n > 0) {
            inputBuffer_.append(buf, n);
        } else if (n == 0) {
            handleClose();  // 对端关闭
            break;
        } else {
            if (errno == EAGAIN) break;  // 读完了
            handleError();
        }
    }
}
```

**面试要点**：
- Epoll 和 Select/Poll 的区别？（O(1) vs O(n)）
- LT 和 ET 的区别？（触发时机）
- 为什么 ET 更高效？（减少系统调用）

### 5.2 TCP 粘包处理

#### 问题

TCP 是字节流协议，没有消息边界

```
发送：[Msg1][Msg2][Msg3]
接收：[Msg1Msg2][Msg3]  // 粘包
接收：[Msg][1Msg2Msg3]  // 半包
```

#### 解决方案：长度前缀

```
┌────────────┬────────────┬────────────┬──────────────┐
│   Length   │    Type    │    Seq     │     Body     │
│  (4 bytes) │ (4 bytes)  │ (4 bytes)  │  (N bytes)   │
└────────────┴────────────┴────────────┴──────────────┘
```

**实现**：
```cpp
void Codec::decode(Buffer* buf) {
    while (buf->readableBytes() >= HEADER_SIZE) {
        // 1. 读取长度
        uint32_t len = buf->peekUint32();
        
        // 2. 检查是否完整
        if (buf->readableBytes() < HEADER_SIZE + len) {
            break;  // 半包，等待更多数据
        }
        
        // 3. 解析消息
        MsgHead head;
        head.len = buf->readUint32();
        head.type = buf->readUint32();
        head.seq = buf->readUint32();
        std::string body = buf->readString(head.len);
        
        // 4. 回调
        messageCallback_(conn, head, body);
    }
}
```

**面试要点**：
- 为什么会粘包？（TCP 字节流特性）
- 如何解决？（长度前缀、分隔符、固定长度）
- 为什么用长度前缀？（高效、支持二进制）

### 5.3 心跳保活

#### 为什么需要心跳？

**问题**：
- TCP 连接可能"假死"（网络故障、对端崩溃）
- 无法及时发现断线

**解决**：
- 应用层心跳：定期发送心跳包
- TCP KeepAlive：系统层保活（不够及时）

#### 本项目实现

```cpp
// 客户端：每 1 秒发送心跳
void SimulatedAgv::UpdateLogic() {
    auto now = std::chrono::steady_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastHeartbeatTime_).count();
    
    if (isLogged_ && diff > 1000) {
        SendHeartbeat();
        lastHeartbeatTime_ = now;
    }
}

// 服务器：更新最后心跳时间
void WorldManager::OnHeartbeat(const Heartbeat& msg) {
    auto it = onlineAgvs_.find(msg.agvId);
    if (it != onlineAgvs_.end()) {
        it->second.lastHeartbeatTime = Timestamp::now().toMilliseconds();
    }
}

// 服务器：定期检查超时（可选）
void AgvManager::CheckTimeout() {
    int64_t now = Timestamp::now().toMilliseconds();
    for (auto& [id, agv] : onlineAgvs_) {
        if (now - agv.lastHeartbeatTime > 10000) {  // 10 秒超时
            LOG_WARN("AGV %d timeout", id);
            removeAgv(id);
        }
    }
}
```

**面试要点**：
- 心跳间隔如何选择？（1-5 秒，平衡及时性和开销）
- 超时时间如何选择？（3-5 倍心跳间隔）
- TCP KeepAlive 为什么不够？（默认 2 小时，太慢）

---

## 6. 并发编程要点

### 6.1 锁的选择

#### 互斥锁（Mutex）

**使用场景**：保护临界区

```cpp
std::mutex mutex_;
std::lock_guard<std::mutex> lock(mutex_);  // RAII
```

**优缺点**：
- ✅ 简单、通用
- ❌ 读写都互斥，性能差

#### 读写锁（Shared Mutex）

**使用场景**：读多写少

```cpp
std::shared_mutex mutex_;

// 读锁（共享）
std::shared_lock<std::shared_mutex> lock(mutex_);

// 写锁（独占）
std::unique_lock<std::shared_mutex> lock(mutex_);
```

**本项目应用**：
```cpp
class WorldManager {
    mutable std::shared_mutex agvMutex_;
    
    // 读操作：多个线程可以同时读
    std::vector<AgvInfo> GetAllAgvs() const {
        std::shared_lock<std::shared_mutex> lock(agvMutex_);
        return std::vector<AgvInfo>(onlineAgvs_.begin(), onlineAgvs_.end());
    }
    
    // 写操作：独占访问
    void OnHeartbeat(const Heartbeat& msg) {
        std::unique_lock<std::shared_mutex> lock(agvMutex_);
        onlineAgvs_[msg.agvId].lastHeartbeatTime = now();
    }
};
```

**面试要点**：
- 读写锁的优势？（读不互斥，提高并发）
- 何时用读写锁？（读多写少）
- 写锁会饿死吗？（可能，需要公平策略）

#### 自旋锁（Spin Lock）

**使用场景**：临界区很短

```cpp
std::atomic_flag lock = ATOMIC_FLAG_INIT;

while (lock.test_and_set(std::memory_order_acquire)) {
    // 自旋等待
}
// 临界区
lock.clear(std::memory_order_release);
```

**优缺点**：
- ✅ 无系统调用，快
- ❌ 占用 CPU，不适合长临界区

### 6.2 无锁编程

#### 原子操作

**使用场景**：简单的计数、标志位

```cpp
std::atomic<int> counter{0};
counter.fetch_add(1);  // 原子加 1

std::atomic<bool> running{true};
running.store(false);  // 原子写
```

**本项目应用**：
```cpp
// 序列号生成
std::atomic<int32_t> nextSeq{0};
int32_t GetNextSeq() {
    return ++nextSeq;  // 原子递增
}

// 任务 ID 生成
std::atomic<uint64_t> taskSeq_{0};
std::string GenerateTaskId() {
    return "T-" + std::to_string(++taskSeq_);
}
```

**面试要点**：
- 原子操作的原理？（CPU 指令，如 LOCK CMPXCHG）
- 和锁的区别？（无系统调用，更快）
- 内存序的作用？（控制可见性和顺序）

### 6.3 线程安全设计

#### 无锁快照（Snapshot）

**问题**：调度算法耗时，不能长时间持锁

**解决**：快照 + 双重检查

```cpp
void TaskManager::TryDispatch() {
    // 1. 快速获取快照（锁内）
    std::vector<TaskContext> tasksSnapshot;
    std::vector<AgvInfo> agvsSnapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasksSnapshot = pendingTasks_;
        agvsSnapshot = WorldMgr.GetAllAgvs();
    }
    
    // 2. 耗时计算（锁外）
    auto decisions = scheduler_->Dispatch(tasksSnapshot, agvsSnapshot);
    
    // 3. 应用决策（锁内 + 双重检查）
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& dec : decisions) {
            // Double Check：状态可能已变化
            if (WorldMgr.GetAgvStatus(dec.agvId) != IDLE) continue;
            if (runningTasks_.count(dec.agvId)) continue;
            
            // 应用决策
            dispatchTask(dec);
        }
    }
}
```

**面试要点**：
- 为什么需要快照？（减少锁持有时间）
- 为什么需要双重检查？（快照可能过期）
- 如何保证一致性？（CAS、版本号）

---

## 7. 算法设计要点

### 7.1 A* 优化

#### 内存池

**问题**：频繁 new/delete 节点，性能差

**解决**：预分配内存池

```cpp
class AStarSolver {
    boost::object_pool<AStarNode> nodePool_;  // 内存池
    std::vector<AStarNode*> usedNodes_;       // 已使用节点
    
    AStarNode* createNode(const Point& pos) {
        AStarNode* node = nodePool_.construct();  // 从池中分配
        node->pos = pos;
        usedNodes_.push_back(node);
        return node;
    }
    
    void ReleaseNodes() {
        for (auto* node : usedNodes_) {
            nodePool_.destroy(node);  // 归还到池
        }
        usedNodes_.clear();
    }
};
```

**优势**：
- 减少系统调用
- 减少内存碎片
- 提高缓存命中率

### 7.2 调度优化

#### 负载均衡

**问题**：贪心算法可能导致某些 AGV 过载

**改进**：考虑 AGV 当前负载

```cpp
int CalculateCost(const AgvInfo& agv, const Task& task) {
    int distance = CalcDistance(agv.pos, task.target);
    int load = agv.taskQueue.size();  // 当前任务数
    return distance + load * 10;  // 加权
}
```

#### 预测优化

**问题**：只考虑当前任务，不考虑未来

**改进**：预测未来任务分布

```cpp
int PredictCost(const AgvInfo& agv, const Task& task) {
    int currentCost = CalcDistance(agv.pos, task.target);
    int futureCost = EstimateFutureCost(task.target);  // 预测
    return currentCost + futureCost * 0.5;
}
```

---

## 8. 面试常见问题

### 8.1 项目整体

**Q1：为什么选择 C++ 而不是 Java/Python？**

A：
- **性能**：C++ 无 GC，延迟可控，适合实时系统
- **底层控制**：直接操作内存、网络，优化空间大
- **资源占用**：内存占用小，适合嵌入式设备
- **生态**：网络库（muduo）、算法库（Boost）成熟

**Q2：系统的瓶颈在哪里？如何优化？**

A：
- **网络 IO**：使用 Epoll ET 模式，减少系统调用
- **锁竞争**：读写锁、无锁快照、细粒度锁
- **内存分配**：内存池、对象池
- **算法复杂度**：A* 启发式、贪心调度

**Q3：如何保证系统的可靠性？**

A：
- **异常处理**：RAII、异常安全
- **超时机制**：心跳保活、RPC 超时
- **日志监控**：异步日志、性能指标
- **优雅退出**：信号处理、资源清理

### 8.2 网络编程

**Q4：Reactor 和 Proactor 的区别？**

A：
- **Reactor**：同步 IO，事件驱动，Linux 常用
- **Proactor**：异步 IO，完成驱动，Windows 常用
- **本项目**：Reactor（Epoll）

**Q5：如何处理大量连接？**

A：
- **主从 Reactor**：分离 Accept 和 IO
- **线程池**：复用线程，避免创建开销
- **Epoll**：O(1) 复杂度，支持百万连接

**Q6：如何防止 DDoS 攻击？**

A：
- **连接限制**：最大连接数、单 IP 限制
- **超时机制**：空闲连接超时断开
- **流量控制**：令牌桶、漏桶算法

### 8.3 并发编程

**Q7：如何避免死锁？**

A：
- **加锁顺序**：统一加锁顺序
- **超时机制**：try_lock_for
- **避免嵌套锁**：减少锁的粒度
- **使用 RAII**：自动解锁

**Q8：读写锁的实现原理？**

A：
- **读计数器**：记录读者数量
- **写标志**：标记是否有写者
- **条件变量**：读者/写者等待队列
- **公平性**：防止写者饿死

**Q9：无锁编程的难点？**

A：
- **ABA 问题**：CAS 的陷阱
- **内存序**：可见性和顺序性
- **调试困难**：竞态条件难复现

### 8.4 算法设计

**Q10：A* 如何保证最优？**

A：
- **启发函数**：h(n) 不高估实际代价
- **单调性**：h(n) ≤ cost(n, n') + h(n')
- **本项目**：曼哈顿距离满足条件

**Q11：如何处理动态障碍物？**

A：
- **实时检测**：AGV 移动前检查
- **路径重规划**：遇到障碍重新规划
- **预测避让**：预测其他 AGV 路径

**Q12：调度算法如何改进？**

A：
- **全局优化**：匈牙利算法、遗传算法
- **负载均衡**：考虑 AGV 当前负载
- **预测优化**：考虑未来任务分布
- **强化学习**：自适应学习最优策略

---

## 9. 项目亮点总结

### 9.1 技术亮点

1. **高性能网络框架**
   - 主从 Reactor 模式
   - Epoll ET 模式
   - 零拷贝优化

2. **智能调度算法**
   - 贪心调度
   - 策略模式（可插拔）
   - 无锁快照优化

3. **高效路径规划**
   - A* 算法
   - 内存池优化
   - 动态重规划

4. **异步日志系统**
   - 双缓冲队列
   - 生产者-消费者
   - 不阻塞主线程

5. **并发控制**
   - 读写锁
   - 无锁编程
   - 线程池

### 9.2 工程亮点

1. **模块化设计**
   - 分层架构
   - 接口抽象
   - 低耦合高内聚

2. **配置化**
   - JSON 配置文件
   - 灵活调整参数
   - 支持多场景

3. **可扩展性**
   - 策略模式
   - 工厂模式
   - 插件化

4. **代码质量**
   - Google C++ Style
   - RAII 资源管理
   - 异常安全

### 9.3 面试话术

**开场白**：
"我做了一个 AGV 智能调度系统，模拟仓储物流场景。系统采用 C++17 开发，使用 Reactor 网络模型和线程池实现高并发，支持 100+ AGV 同时在线。核心功能包括动态任务调度、A* 路径规划和实时避障。"

**技术深入**：
"在网络层，我使用了主从 Reactor 模式，主 Reactor 负责 Accept，从 Reactor 负责 IO，充分利用多核 CPU。在调度层，我实现了贪心调度算法，并使用策略模式支持算法热插拔。在规划层，我使用 A* 算法进行路径规划，并通过内存池优化性能。"

**亮点强调**：
"项目的亮点在于：一是高性能，通过 Epoll ET 模式和零拷贝优化，支持高并发；二是智能调度，通过无锁快照和双重检查，保证调度的正确性和效率；三是可扩展，通过策略模式和配置化设计，支持灵活扩展。"

**结尾**：
"通过这个项目，我深入理解了网络编程、并发编程和算法设计，也积累了 C++ 工程实践经验。"

---

## 附录：快速复习清单

### C++ 核心

- [ ] 智能指针（shared_ptr、weak_ptr、unique_ptr）
- [ ] RAII（资源管理、异常安全）
- [ ] 移动语义（右值引用、std::move）
- [ ] 模板编程（策略模式、SFINAE）

### 网络编程

- [ ] IO 多路复用（Epoll、LT/ET）
- [ ] Reactor 模式（主从 Reactor）
- [ ] TCP 协议（三次握手、粘包处理）
- [ ] 心跳保活（应用层心跳）

### 并发编程

- [ ] 线程池（任务队列、条件变量）
- [ ] 锁（互斥锁、读写锁、自旋锁）
- [ ] 无锁编程（原子操作、内存序）
- [ ] 死锁（避免方法、检测方法）

### 算法设计

- [ ] A* 算法（启发式搜索、优先队列）
- [ ] 贪心算法（局部最优、时间复杂度）
- [ ] 优化技巧（内存池、快照、预测）

---

**祝你面试顺利！🎉**
