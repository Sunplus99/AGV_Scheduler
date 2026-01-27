#pragma once

#include <cstdint>
#include <string>


namespace agv{
namespace config{

// 地图加载模式
enum class MapType {
    DEFAULT,
    FILE,
    RANDOM
};

struct MapConfig{
    MapType type = MapType::DEFAULT;
    std::string path = "";
    int width = 20;
    int height = 20;
    double obstacleRatio = 0.1;
};

struct ServerConfig{
    // 网络配置
    std::string ip = "0.0.0.0"; // 通配地址
    uint16_t port = 8888;
    int tcpTimeoutSec = 60;

    // 线程配置
    int ioThreadnum = 2;
    int workerThreadnum = 2;

    // 业务配置
    int rpcTimeoutMs = 5000;

    // 地图配置
    MapConfig map;
};


    
}
}
