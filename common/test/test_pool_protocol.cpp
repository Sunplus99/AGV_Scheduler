#include "protocol/protocol.h"
#include <iostream>
#include <vector>
#include <thread>

void test_allocation() {
    std::cout << "Thread [" << std::this_thread::get_id() << "] starting..." << std::endl;

    // 1. 创建消息
    // 这一步会自动调用 AgvMessage::operator new 
    // 进而触发 thread_local 的 ObjectPool::allocate
    AgvMessage* msg = new AgvMessage(); 
    
    // 2. 填充数据
    msg->setData(MsgType::LOGIN_REQ, "AGV-001 Login");

    std::cout << "Thread [" << std::this_thread::get_id() << "] "
              << "Allocated Msg at: " << msg 
              << ", Type: " << msg->head.type 
              << ", Body: " << msg->body << std::endl;

    // 3. 销毁消息
    // 这一步自动调用 AgvMessage::operator delete -> pool.deallocate
    delete msg; 
}

int main() {
    std::cout << "=== Server Start: Testing Protocol & Memory Pool ===" << std::endl;

    // 模拟 Reactor 的多线程环境
    std::vector<std::thread> threads;
    for(int i = 0; i < 3; ++i) {
        threads.emplace_back(test_allocation);
    }

    for(auto& t : threads) {
        t.join();
    }

    std::cout << "=== Test Finished ===" << std::endl;
    return 0;
}