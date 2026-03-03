# AGV 调度服务器 (基于自研 Reactor 多线程网络模型)

> 本项目是基于 Modern C++ 实现的 AGV 调度系统核心服务，初衷是将车联网资源调度研究中的核心思想（如资源分配）适配到工业仓储 AGV 调度的工程场景中，验证自研 Reactor 网络框架的工程可行性与基础调度逻辑的落地效果。系统实现了从底层自研多线程网络框架到上层任务调度的完整功能链路。

[![Language](https://img.shields.io/badge/language-Modern_C%2B%2B-blue.svg)](https://isocpp.org/)
[![Build](https://img.shields.io/badge/build-CMake-green.svg)](https://cmake.org/)
[![License](https://img.shields.io/badge/license-MIT-orange.svg)](LICENSE)



## 架构与核心模块

系统采用**三层解耦架构**，模块职责清晰，兼顾性能与可维护性：

### 1. 网络层（自研 Reactor 框架）

- **多线程模型**：基于 Epoll 边缘触发(ET)模式实现主从 Reactor 架构，封装非阻塞 IO 处理网络事件。
- **职责分离**：IO 事件处理与业务 Worker 线程池解耦，避免路径规划等耗时逻辑阻塞网络层。
- **网络优化**：优化 Buffer 复用逻辑，基于自定义协议处理 TCP 粘包、半包问题。

### 2. 基础组件层（Common）

- **通信协议（AgvCodec）**：参考 VDA 5050 协议结构，实现 `4字节包头 + JSON 包体` 的自定义协议编解码。
- **内存管理（ObjectPool）**：实现泛型对象池，复用高频小对象（消息包、任务节点），降低内存分配开销与碎片。
- **异步日志（AsyncLogging）**：基于双缓冲机制与多生产者-单消费者模型实现前后端分离，通过后台独立线程异步批量落盘。



### 3. 业务调度层（AGVServer）

- **状态与会话管理**：实现 `WorldManager`（地图与实体状态）与 `TaskManager`（任务队列管理）；核心设计了 `AgvManager` 模块维护网络层 (`Connection`) 与业务层 (`AgvId`) 到 `AgvSession` 的双向映射，保障底层网络事件与上层业务状态的强一致性。
- **基础算法落地实现**：基于面向接口编程设计可插拔的寻路与调度接口，支持 A* 与贪心算法的运行时热切换；寻路底层结合`thread_local` 线程隔离与对象池技术复用计算节点，降低高并发下的锁竞争与内存分配开销。

---

## 性能测试数据

测试环境：Ubuntu 20.04 虚拟机（4核CPU / 4GB 内存），详细压测方案见 [PERF_REPORT.md](PERF_REPORT.md)：

- **并发连接**：单机稳定维持 10,000 个空闲长连接，无中断、无崩溃。
- **吞吐量**：64B 报文、100 线程并发 Ping-Pong 压测，峰值 QPS 约 4.5 万。
- **延迟分布**：单连接 10,000 次请求，P50 延迟≈0.8ms，P99 延迟＜3ms，无明显长尾请求。
- **调度耗时**：基准场景（30台 AGV、100×100 地图、20%障碍物率），A* 路径规划 P99=0.88ms，贪心调度 P99=0.12ms，均在 1ms 内。

---

## 快速开始

### 环境要求

- **操作系统**：Linux（Ubuntu 18.04+ / CentOS 7+）
- **编译器**：GCC 7.5+（支持 Modern C++）
- **构建工具**：CMake 3.10+

### 编译与运行

```bash
# 1. 编译项目
mkdir -p build && cd build
cmake .. && make -j$(nproc)

# 2. 启动服务端（WARN 日志级别，减少磁盘 IO 开销）
cd ../bin
./AgvServer WARN

# 3. 启动客户端模拟器（模拟 30 台 AGV 接入调度）
./AgvSimulator 30
```