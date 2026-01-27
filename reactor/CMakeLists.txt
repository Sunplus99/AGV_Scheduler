# 独立的逻辑模块,子项目
#[[
project(AGVReactor) 的作用： (在VS或者Clion可见，linux不可见)
    IDE 视图整理： 如果你用 Visual Studio 或 CLion 打开整个项目，有了这句，IDE 会把 reactor 下的所有目标（库、可执行文件）自动归类到一个叫 AGVReactor 的文件夹里，而不是散落在根目录下。看着更清爽。
    变量隔离： 定义了 project 后，CMake 会自动定义 ${PROJECT_SOURCE_DIR}。
        如果不加：${PROJECT_SOURCE_DIR} 指向根目录（AGVScheduler/）。
        如果加了：在这个文件里，${PROJECT_SOURCE_DIR} 指向 AGVScheduler/reactor/。这让你写路径时更不容易出错。
]]
cmake_minimum_required(VERSION 3.10)
project(AGVReactor LANGUAGES CXX)   

# -------------------------------------------------------
# 1st.构建核心网络库 (libreactor.a)
# -------------------------------------------------------

# ① 搜集 src 下的所有 .cpp 文件
file(GLOB REACTOR_SRC "src/*.cpp") 
# aux_source_directory(./src REACTOR_SRC)

# ② 生成静态库
add_library(myreactor_static STATIC ${REACTOR_SRC})  # ← 编译成 libmylib.a

# ③ 配置头文件搜索路径
# PUBLIC: 
#   1. 编译库自己时，去 ./include 找 
#   2. 别人链接这个库时，自动继承这个路径 
target_include_directories(myreactor_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ④ 链接必要的系统库
target_link_libraries(myreactor_static pthread)    


#[[
# -------------------------------------------------------
# 2st.构建测试程序 (tcpepoll.exe)
# -------------------------------------------------------

# ① 定义可执行文件
add_executable(tcpepoll 
    example/tcpepoll.cpp
    example/EchoServer.cpp
)

# ② 链接核心库   （如果是PUBLIC,会继承头文件路径）
target_link_libraries(tcpepoll myreactor_static)  # ← 链接二进制文件

# ③ 指定自己的头文件（ EchoServer.h）路径
target_include_directories(tcpepoll PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/example)
]]

