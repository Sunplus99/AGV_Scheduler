cmake_minimum_required(VERSION 3.10)
project(AGV_Scheduler LANGUAGES CXX)

# =========================================================
# 1. 基础编译设置
# =========================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启调试信息和所有警告
add_compile_options(-Wall -g -pthread)

# 设置输出目录 (编译出来的 .a 在 lib, 可执行程序在 bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# =========================================================
# 2. 全局头文件路径 (Include Paths)
# =========================================================
include_directories(
    ${CMAKE_SOURCE_DIR}/common/include
    ${CMAKE_SOURCE_DIR}/reactor/include
    ${CMAKE_SOURCE_DIR}/server/include
)

# =========================================================
# 3. 模块一：Common 库 (协议、工具、模型)
# =========================================================
# GLOB_RECURSE：递归匹配文件
file(GLOB_RECURSE COMMON_SRC "${CMAKE_SOURCE_DIR}/common/src/*.cpp")
add_library(agv_common STATIC ${COMMON_SRC})

# =========================================================
# 4. 模块二：Reactor 库 (网络底层)
# =========================================================
file(GLOB_RECURSE REACTOR_SRC "${CMAKE_SOURCE_DIR}/reactor/src/*.cpp")
add_library(myreactor STATIC ${REACTOR_SRC})
# 网络库依赖线程库
target_link_libraries(myreactor pthread)

# =========================================================
# 5. 模块三：Server 业务逻辑库 (核心逻辑，不含 main)
# =========================================================
file(GLOB_RECURSE SERVER_SRC "${CMAKE_SOURCE_DIR}/server/src/*.cpp")

# 定位 main.cpp
set(SERVER_MAIN "${CMAKE_SOURCE_DIR}/server/src/main.cpp")

# 从源文件列表中剔除 main.cpp
if(EXISTS ${SERVER_MAIN})
    list(REMOVE_ITEM SERVER_SRC ${SERVER_MAIN})
endif()

# 生成业务逻辑静态库
add_library(agv_logic STATIC ${SERVER_SRC})
# 业务层依赖：网络库 + 通用库
target_link_libraries(agv_logic myreactor agv_common)

# =========================================================
# 6. 可执行程序：AgvServer (服务端)
# =========================================================
add_executable(AgvServer ${SERVER_MAIN})

# 链接依赖链：Main -> Logic -> Reactor -> Common -> System Libs
target_link_libraries(AgvServer 
    agv_logic 
    myreactor 
    agv_common 
    pthread 
    dl
)

# =========================================================
# 7. 可执行程序：AgvSimulator (客户端模拟器)
# =========================================================
file(GLOB_RECURSE CLIENT_SRC "${CMAKE_SOURCE_DIR}/client/src/*.cpp")

add_executable(AgvSimulator ${CLIENT_SRC})

# 客户端只需要网络库和通用库，不需要服务端的调度逻辑
target_link_libraries(AgvSimulator
    myreactor
    agv_common
    pthread
    dl    
)
# dl: libdl（Dynamic Linking Library）的缩写，提供动态加载共享库的功能。未来需要插件系统,习惯保留

# =========================================================
# 8. 可执行程序：tcpepoll (Echo Server 性能测试)
# =========================================================
add_executable(tcpepoll
    ${CMAKE_SOURCE_DIR}/reactor/example/tcpepoll.cpp
    ${CMAKE_SOURCE_DIR}/reactor/example/EchoServer.cpp
)

# 链接 Reactor 网络库
target_link_libraries(tcpepoll myreactor pthread)

# 指定 EchoServer.h 的头文件路径
target_include_directories(tcpepoll PRIVATE ${CMAKE_SOURCE_DIR}/reactor/example)

# =========================================================
# 9. 自动化资源部署 (Post-Build Actions)
# =========================================================
# 编译完成后，自动把配置文件拷贝到 bin 目录，方便直接运行
file(COPY ${CMAKE_SOURCE_DIR}/server/config/config.json 
     DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# 自动拷贝地图文件夹
file(COPY ${CMAKE_SOURCE_DIR}/server/maps 
     DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

message(STATUS "Build Configured. Output Path: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")








# cmake_minimum_required(VERSION 3.10)
# project(AGV_Scheduler)

# # 1. 设置 C++ 标准
# set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)

# # 2. 输出路径设置 (让编译出来的程序整齐一点)
# set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
# set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# # =========================================================
# # 3. 核心：头文件路径 (Include Paths)
# # 只要这里写全了，代码里 #include 也就不会报错了
# # =========================================================
# include_directories(
#     ${CMAKE_SOURCE_DIR}/common/include      # 对应 #include "utils/Logger.h" 等
#     ${CMAKE_SOURCE_DIR}/reactor/include     # 对应 #include "myreactor/TcpServer.h"
#     ${CMAKE_SOURCE_DIR}/server/include      # 对应 #include "session/AgvManager.h"
# )

# # =========================================================
# # 4. 模块一：Common 库 (工具类)
# # =========================================================
# # 搜集 common/src 下的所有 .cpp
# file(GLOB_RECURSE COMMON_SRC "${CMAKE_SOURCE_DIR}/common/src/*.cpp")
# # 生成静态库 libagv_common.a
# add_library(agv_common STATIC ${COMMON_SRC})

# # =========================================================
# # 5. 模块二：Reactor 库 (网络库)
# # =========================================================
# # 搜集 reactor/src 下的所有 .cpp
# file(GLOB_RECURSE REACTOR_SRC "${CMAKE_SOURCE_DIR}/reactor/src/*.cpp")
# # 生成静态库 libmyreactor.a
# add_library(myreactor STATIC ${REACTOR_SRC})
# # 网络库通常依赖 pthread
# target_link_libraries(myreactor pthread)

# # =========================================================
# # 6. 模块三：Server 业务逻辑 (你的核心代码)
# # =========================================================
# # 搜集 server/src 下的所有 .cpp (包括 manager, session, algo 等子文件夹)
# # GLOB_RECURSE 会自动钻进子目录去找文件，非常适合你现在的结构
# file(GLOB_RECURSE SERVER_SRC "${CMAKE_SOURCE_DIR}/server/src/*.cpp")

# # 生成静态库 libagv_logic.a (先把业务编成库，方便管理)
# add_library(agv_logic STATIC ${SERVER_SRC})
# # 业务库依赖网络库和工具库
# target_link_libraries(agv_logic myreactor agv_common)

# # =========================================================
# # 7. 最终产物：可执行程序
# # =========================================================
# add_executable(AgvServer main.cpp)

# # 链接所有依赖
# target_link_libraries(AgvServer 
#     agv_logic     # 你的业务
#     myreactor     # 网络库
#     agv_common    # 工具库
#     pthread       # 线程支持
#     dl            # 动态加载支持(Linux保险起见)
# )

# # =========================================================
# # (可选) 客户端测试程序
# # 如果你想顺便把 client 目录下的也编译了，可以解开下面注释
# # =========================================================
# file(GLOB_RECURSE CLIENT_SRC "${CMAKE_SOURCE_DIR}/client/*.cpp")
# add_executable(AgvClient ${CLIENT_SRC})
# target_link_libraries(AgvClient myreactor agv_common pthread)