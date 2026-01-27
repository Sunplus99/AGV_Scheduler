cmake_minimum_required(VERSION 3.10)
project(AGVScheduler
        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)           # “我希望你用 C++17 编译”。如果编译器版本太老不支持 17，CMake 可能会默默地降级到 C++14 或 C++11 试着编译。
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # 加上 REQUIRED ON 后：如果编译器不支持 C++17，CMake 会直接报错停止（Error），而不是降级。

# 设置默认构建类型
# 逻辑：如果用户在命令行没指定类型，默认使用 Debug 模式 -g
#[[手动指定 Release 模式
    cmake -DCMAKE_BUILD_TYPE=Release
    此时生成的 tcpepoll 是经过 -O3 极致优化的，且不带调试符号，体积更小，速度飞快]]
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

#[[STATUS：日志级别（Mode）。
    STATUS：最常用的。输出时前面会带个 -- 前缀，表示“普通信息，一切正常”。
    WARNING：输出红字/黄字警告，但不停止编译。
    FATAL_ERROR：输出红字错误，立即停止编译（比如检测到没有装 g++ 时用）。
    (如果不写级别，直接写内容，就是最普通的打印)]]
message(STATUS "--------------------------------------------------")
message(STATUS "当前构建类型：${CMAKE_BUILD_TYPE}")
message(STATUS "--------------------------------------------------")

# 全局通用编译选项 (建议加上 -Wall 开启警告)
# 这些选项对 Debug 和 Release 都生效
add_compile_options(
    -Wall          # 开启所有警告
    -Wextra        # 额外警告
    -Wpedantic     # 严格模式
)

# 设置构建产物的统一输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 添加子目录
add_subdirectory(reactor)
# add_subdirectory(common)
# add_subdirectory(server)
# add_subdirectory(client)
