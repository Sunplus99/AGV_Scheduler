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
            /* >> 重载
            原生的 std::ifstream 自带的 >> 运算符是 “按空白符（空格 / 换行 / 制表符）分割读取”（比如读单个字符串、数字），但 nlohmann/json 库专门重载了 operator>>，适配 std::istream（包括 ifstream）和 json 对象：
                它会读取文件流中全部剩余内容并解析为 JSON 结构，最终将解析后的数据拷贝（而非转移） 到 j 对象中；文件本身的内容不会被修改 / 转移，只是从磁盘读取到内存并复制到 j 的内存空间里。
            */
            json j;
            ifs >> j;  

            /*
            j["server"]["port"] 是 nlohmann/json 的嵌套键链式访问语法，通过重载的 operator[] 直接读取多层 JSON 节点的值，无存在性检查和默认值，键缺失 / 类型不匹配会抛异常，适用于必选配置；而 value(key, def) 是安全读取方式，键缺失时返回默认值，不抛异常，适用于可选配置 —— 两者核心区别是是否支持默认值兜底、是否容忍键缺失。
                用 链式[] 的场景:配置项是程序运行的核心依赖，缺失则必须终止并提示错误（比如数据库密码、核心服务端口）：
                    // 示例：数据库密码是必选，缺失则抛异常
                        try {
                            outConfig.dbPwd = j["db"]["password"];
                        } catch (const std::exception& e) {
                            LOG_ERROR("Missing required config: db.password, %s", e.what());
                            return false;
                        }
                
                用 value() 的场景:配置项是可选的，缺失时用默认值即可（比如 ip/port/tcp_timeout 等）：
                    // 示例：port 可选，缺失用 8888
                        outConfig.port = s.value("port", (uint16_t)8888);
            */
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
            /*常引用捕获
            在 C++ 异常处理中，捕获std::exception及其子类时使用const std::exception&（即catch (const std::exception& e)）是行业通用的最佳实践，核心原因可总结为三点，既保证代码的正确性，也符合工业级开发规范：
            第一，避免拷贝与二次异常。异常对象往往包含错误上下文（比如 JSON 解析的行号、具体语法错误），若用值捕获（catch (std::exception e)）会触发拷贝构造函数，既消耗性能，更危险的是如果拷贝构造本身抛出异常，会直接导致程序终止；而引用捕获直接指向原始异常对象，无任何拷贝，能稳定执行错误日志、止损等逻辑。
            第二，保留多态信息，精准定位错误。实际抛出的异常通常是std::exception的子类（比如 JSON 解析库的parse_error），这些子类会重写what()方法返回具体错误。值捕获会发生 “对象切片”，丢失子类特有信息，只能拿到通用错误；引用捕获则能完整保留子类特性，调用what()可获取精准的错误详情（比如 “JSON 第 5 行语法错误”），大幅降低调试成本。
            第三，const 修饰保证安全与语法合法。一方面，const 强制异常对象只读，避免误修改错误现场；另一方面，C++ 语法规定临时异常对象（如throw parse_error("xxx")）只能绑定到 const 引用，去掉 const 会直接编译报错，无法捕获异常。
            综上，const引用捕获异常是兼顾性能、正确性、语法合法性的最优选择
            */
            LOG_ERROR("JSON Prase Error: %s", e.what());
            return false;
        }


        /*
        错误吞噬 (Error Swallowing)：
            这里没有 throw;。
            这意味着：配置加载失败不是致命错误（或者虽然致命，但我们希望由 main 函数通过返回值判断来优雅退出，而不是由运行时系统暴力终止）。
            我们选择消化掉这个异常，把它转化为一个 false 的返回值，让程序逻辑更平滑。
        */
    }
};

}
}