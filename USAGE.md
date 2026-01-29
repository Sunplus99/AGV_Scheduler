# AGV 调度系统使用指南

## 快速开始

### 1. 编译

```bash
cd AGVScheduler
mkdir -p build && cd build
cmake .. && make -j$(nproc)
cd ../bin
```

### 2. 运行

#### 方式 1：默认配置（推荐演示）

```bash
# 终端 1：启动服务器（WARN 级别，减少日志）
./AgvServer ./config.json WARN

# 终端 2：启动 10 辆 AGV
./AgvSimulator 10
```

#### 方式 2：自定义日志级别

```bash
# DEBUG：最详细（调试用）
./AgvServer ./config.json DEBUG

# INFO：一般信息（默认）
./AgvServer ./config.json INFO

# WARN：只显示警告（推荐演示）
./AgvServer ./config.json WARN

# ERROR：只显示错误
./AgvServer ./config.json ERROR
```

#### 方式 3：自定义 AGV 数量

```bash
# 3 辆 AGV（小规模）
./AgvSimulator 3

# 10 辆 AGV（中等规模，推荐）
./AgvSimulator 10

# 30 辆 AGV（压力测试）
./AgvSimulator 30
```

---

## 配置文件说明

### config.json

```json
{
    "server": {
        "ip": "0.0.0.0",           // 监听地址
        "port": 8888,              // 监听端口
        "tcp_timeout_s": 60,       // TCP 超时（秒）
        "rpc_timeout_ms": 5000,    // RPC 超时（毫秒）
        "threads_num": {
            "io": 4,               // IO 线程数（建议 CPU 核心数）
            "worker": 4            // Worker 线程数（建议 CPU 核心数）
        }
    },
    "map": {
        "type": "RANDOM",          // 地图类型：DEFAULT/RANDOM/FILE
        "width": 50,               // 地图宽度
        "height": 50,              // 地图高度
        "ratio": 0.15              // 障碍物比例（0.0-1.0）
    }
}
```

### 推荐配置

#### 小规模演示（3 辆 AGV）

```json
{
    "map": {
        "type": "DEFAULT",
        "width": 10,
        "height": 10,
        "ratio": 0.0
    },
    "server": {
        "threads_num": {
            "io": 2,
            "worker": 2
        }
    }
}
```

#### 中等规模展示（10 辆 AGV）⭐ 推荐校招

```json
{
    "map": {
        "type": "RANDOM",
        "width": 50,
        "height": 50,
        "ratio": 0.15
    },
    "server": {
        "threads_num": {
            "io": 4,
            "worker": 4
        }
    }
}
```

#### 压力测试（30 辆 AGV）

```json
{
    "map": {
        "type": "RANDOM",
        "width": 100,
        "height": 100,
        "ratio": 0.1
    },
    "server": {
        "threads_num": {
            "io": 8,
            "worker": 8
        }
    }
}
```

---

## 日志查看

### 终端日志

日志会实时显示在终端，带颜色区分：
- 🟢 绿色：INFO
- 🟡 黄色：WARN
- 🔴 红色：ERROR

### 文件日志

日志同时保存到文件：`./logs/agv_server.log`

```bash
# 实时查看日志
tail -f ./logs/agv_server.log

# 查看最近 100 行
tail -100 ./logs/agv_server.log

# 搜索关键词
grep "Dispatch Success" ./logs/agv_server.log
grep "COMPLETED" ./logs/agv_server.log
grep "ERROR" ./logs/agv_server.log
```

---

## 常见问题

### Q1：端口被占用

```bash
# 查看端口占用
lsof -i:8888

# 杀死进程
kill -9 <PID>
```

### Q2：日志太多，看不清

```bash
# 使用 WARN 级别
./AgvServer ./config.json WARN

# 或者只看文件日志
./AgvServer ./config.json INFO > /dev/null
tail -f ./logs/agv_server.log
```

### Q3：AGV 不移动

检查：
1. 地图是否有障碍物阻挡
2. 任务目标是否在地图范围内
3. 查看日志中的路径规划结果

### Q4：编译错误

```bash
# 检查 GCC 版本（需要 7.5+）
gcc --version

# 检查 CMake 版本（需要 3.10+）
cmake --version

# 清理重新编译
rm -rf build
mkdir build && cd build
cmake .. && make -j$(nproc)
```

---

## 性能监控

### 查看系统资源

```bash
# CPU 使用率
top -p $(pgrep AgvServer)

# 内存使用
ps aux | grep AgvServer

# 网络连接
netstat -anp | grep 8888
```

### 性能指标

正常情况下：
- CPU 使用率：10-30%（10 辆 AGV）
- 内存占用：~50MB
- 任务吞吐：1000+ tasks/min
- 路径规划：<10ms

---

## 停止服务

```bash
# 优雅停止（Ctrl+C）
# 服务器会自动清理资源

# 强制停止
pkill -9 AgvServer
pkill -9 AgvSimulator
```

---

## 演示建议

### 校招面试演示

1. **启动服务器**（WARN 级别）
   ```bash
   ./AgvServer ./config.json WARN
   ```

2. **启动 10 辆 AGV**
   ```bash
   ./AgvSimulator 10
   ```

3. **观察要点**
   - 任务分配日志
   - AGV 移动过程
   - 任务完成统计

4. **讲解要点**
   - Reactor 网络模型
   - A* 路径规划
   - 贪心调度算法
   - 并发控制

---

**祝你演示顺利！🎉**
