#pragma once

#include "ServerConfig.h"
#include "utils/json.hpp"
#include "utils/Logger.h"
#include <fstream>
#include <string>
#include <exception>
#include <cstdint>


using json = nlohmann::json;

namespace agv {
namespace config {

class ConfigLoader{
public:
    static bool Load(const std::string& filePath, ServerConfig& toConfig) {
        try{
            // 打开文件
            std::ifstream ifs(filePath);
            if(!ifs.is_open()){
                LOG_ERROR("Config file not found: %s", filePath.c_str());
                return false;
            }

            // JSON 读取与解析映射
            json j;
            ifs >> j;  

           // 双重保险: 第一次（结构体定义），第二次（JSON 解析）
           if(j.contains("server")) {
                auto& s = j["server"];  // 访问 JSON 子对象
                toConfig.ip = s.value("ip","0.0.0.0");
                toConfig.port = s.value("port",(uint16_t)8888);
                toConfig.tcpTimeoutSec = s.value("tcp_timeout_s",60);
                toConfig.rpcTimeoutMs = s.value("rpc_timeout_ms", 5000);
                // 嵌套访问
                if(s.contains("threads_num")) {
                    toConfig.ioThreadnum = s["threads_num"].value("io", 2);
                    toConfig.workerThreadnum = s["threads_num"].value("worker", 2);
                }
           }

           if(j.contains("map")) {
                auto& m = j["map"];
                std::string typeStr = m.value("type", "DEFAULT");

                // 字符串转枚举
                if (typeStr=="FILE") toConfig.map.type = MapType::FILE;
                else if (typeStr=="RANDOM") toConfig.map.type = MapType::RANDOM;
                else toConfig.map.type = MapType::DEFAULT;

                toConfig.map.path = m.value("path","");
                toConfig.map.width = m.value("width", 50);
                toConfig.map.height = m.value("height", 50);
                toConfig.map.obstacleRatio = m.value("ratio", 0.1);
           }

           LOG_INFO("Config loaded successfully from %s", filePath.c_str());
           return true;

        } catch (const std::exception& e){
            LOG_ERROR("JSON Prase Error: %s", e.what());
            return false;
        }

    }
};

}
}