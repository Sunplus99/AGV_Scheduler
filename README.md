# AGV 智能调度系统

> 一个基于 C++17 的高性能、可扩展的自动导引车（AGV）调度系统，支持多车并发调度、动态路径规划和实时任务分配。

[![Language](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)](https://isocpp.org/)
[![Build](https://img.shields.io/badge/build-CMake-green.svg)](https://cmake.org/)
[![License](https://img.shields.io/badge/license-MIT-orange.svg)](LICENSE)

---

## 📋 项目简介

本项目是一个完整的 AGV 调度系统，模拟了仓储物流场景中多辆 AGV 的协同工作。系统采用 **Reactor 网络模型** + **线程池** 的高并发架构，支持 **动态任务调度**、**A* 路径规划**、**避障重规划** 等核心功能。

### 核心特性

- ✅ **高性能网络框架**：基于 Epoll 的 Reactor 模型，支持高并发连接
- ✅ **智能任务调度**：贪心调度算法，支持热插拔调度策略
- ✅ **动态路径规划**：A* 算法，支持实时避障和路径重规划
- ✅ **异步日志系统**：双缓冲队列，支持文件和终端输出
- ✅ **配置化设计**：JSON 配置文件，支持灵活调整参数
- ✅ **可扩展架构**：支持 100+ AGV 并发调度

---

## 🚀 快速开始

### 环境要求

- **操作系统**：Linux (Ubuntu 18.04+)
- **编译器**：GCC 7.5+ (支持 C++17)
- **构建工具**：CMake 3.10+

### 编译运行

```bash
# 1. 编译
mkdir -p build && cd build
cmake .. && make -j$(nproc)

# 2. 运行服务器（WARN 级别，减少日志）
cd ../bin
./AgvServer ./config.json WARN

# 3. 运行客户端（10 辆 AGV）
./AgvSimulator 10
```

### 日志级别

```bash
./AgvServer [config_path] [log_level]

# 级别：DEBUG < INFO < WARN < ERROR < FATAL
# 推荐演示：WARN（只显示重要信息）
```

---

## 🎯 技术亮点

- **Reactor 网络模型**：主从 Reactor + 线程池
- **A* 路径规划**：启发式搜索 + 动态避障
- **贪心调度算法**：距离优先 + 策略模式
- **异步日志系统**：双缓冲队列 + 生产者-消费者
- **读写锁优化**：`shared_mutex` 多读单写

---

## 📊 性能指标

| 指标 | 数值 |
|------|------|
| 并发 AGV | 100+ |
| 任务吞吐 | 1000+ tasks/min |
| 路径规划 | <10ms (50x50) |
| 调度延迟 | <5ms |

---

<!-- 详细文档请参考 [项目报告](PROJECT_REPORT.md) -->
