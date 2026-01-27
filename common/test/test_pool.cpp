// test_pool.cpp
#include <iostream>
#include <vector>
#include <chrono>
#include "ObjectPool.h"

using namespace std;

// 模拟一个 A* 算法中的节点
struct GridNode {
    int x, y;
    int g, h;
    GridNode* parent;

    GridNode(int _x, int _y) : x(_x), y(_y), g(0), h(0), parent(nullptr) {}
};

// 计时器辅助类
class ScopedTimer {
public:
    ScopedTimer(const string& name) : m_name(name), m_start(chrono::high_resolution_clock::now()) {}
    ~ScopedTimer() {
        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - m_start).count();
        cout << "[" << m_name << "] 耗时: " << duration << " ms" << endl;
    }
private:
    string m_name;
    chrono::time_point<chrono::high_resolution_clock> m_start;
};

const int TEST_COUNT = 1000000; // 100万次分配测试

void test_native_new() {
    ScopedTimer timer("Native New/Delete");
    vector<GridNode*> nodes;
    nodes.reserve(TEST_COUNT);

    for (int i = 0; i < TEST_COUNT; ++i) {
        nodes.push_back(new GridNode(i, i));
    }

    for (auto node : nodes) {
        delete node;
    }
}

void test_object_pool() {
    ScopedTimer timer("Object Pool");
    // 实例化一个 GridNode 的对象池
    ObjectPool<GridNode> pool;
    vector<GridNode*> nodes;
    nodes.reserve(TEST_COUNT);

    for (int i = 0; i < TEST_COUNT; ++i) {
        // 使用 pool.construct 代替 new
        nodes.push_back(pool.construct(i, i));
    }

    for (auto node : nodes) {
        // 使用 pool.destroy 代替 delete
        pool.destroy(node);
    }
}

int main() {
    cout << "=== 性能对比测试: 100万次对象分配与释放 ===" << endl;
    cout << "对象大小: " << sizeof(GridNode) << " bytes" << endl;

    // 1. 测试原生 new/delete
    test_native_new();

    // 2. 测试对象池
    test_object_pool();

    return 0;
}

