# AGV Scheduler (AGV 调度系统服务端)

## 1. 项目简介
本项目是一个基于 C++ 开发的自动化引导车（AGV）调度系统服务端。采用多线程 Reactor 模式处理高并发连接，并实现任务的动态分配与路径规划。

## 2. 核心技术栈
* **语言**: C++17
* **网络**: 自研 Reactor 模式网络库 (非阻塞 IO + Epoll)
* **并发**: 线程池处理计算密集型调度任务
* **核心组件**: 
    * 线程池 (Thread Pool)
    * 内存池 (Object Pool)
    * 双缓冲队列 (Double Buffering Queue)
* **算法**: A* 路径规划; 贪心调度逻辑
* **工具**: CMake 编译管理, GDB 调试

## 3. 如何运行
1. `mkdir build && cd build`
2. `cmake ..`
3. `make`
4. `./bin/AgvServer`