#include "map/GridMap.h"
#include "utils/Logger.h"
#include <fstream> 
#include <iostream>
#include <random>


GridMap::GridMap() : width_(0), height_(0) {}

bool GridMap::LoadMap(const std::string& filename) {
    std::ifstream file(filename);  // 输入文件流类/对象
    if(!file.is_open()) {
        LOG_ERROR("Failed to open map file: %s. Using DEFAULT map.", filename.c_str());
        CreateDefaultMap();
        return false;
    }

    /*
    链式调用：file >> width_ >> height_ 等价于 (file >> width_) >> height_，本质是两次调用流提取运算符：
    先执行 file >> width_：从文件流 file 中读取一个数值，解析为 width_ 的类型（比如 int），并赋值给 width_；
    file >> width_ 的返回值是 file 【对象本身的引用】（std::ifstream&），因此可以继续链式调用 >> height_，完成 height_ 的读取；
    !(...) 取反的核心 —— 流状态的布尔转换
    流对象（如 file）在「需要布尔值的上下文（比如 if 条件）」中，会触发隐式布尔转换：
    */
    // 读取高宽
    if(!(file >> width_ >> height_)) {
        LOG_ERROR("Map file (%s) format error: header missing.\nUsing DEFAULT map.",filename.c_str());
        CreateDefaultMap();
        return false;
    }

    // 读取栅格数据
    grid_.resize(height_, std::vector<int>(width_));

    for(int y = 0 ; y < height_; ++y) {
        for(int x= 0 ; x < width_ ; ++x) {
            int val;
            file >> val;
            grid_[y][x] = val;
        }
    }

    // 收尾
    file.close();
    LOG_INFO("Map loaded successfully from %s (%dx%d)",filename.c_str(), width_, height_);
    return true;
}

void GridMap::CreateDefaultMap() {
    // 这是一个 10x10 的兜底地图，四周是墙，中间空
    width_ = 10;
    height_ = 10;
    grid_.assign(height_, std::vector<int>(width_, 0));

    // 简单造个围墙
    for(int i=0; i<10; ++i) {
        grid_[0][i] = 1;      // 上墙
        grid_[9][i] = 1;      // 下墙
        grid_[i][0] = 1;      // 左墙
        grid_[i][9] = 1;      // 右墙
    }
    LOG_WARN("Default Map Created.");
}

/*
如果你的地图是 1000x1000，面试官可能会问这类高质量问题，这也是你拿 Offer 的机会：

内存问题：
    “你的 grid_ 用 vector<vector<int>> 存储，在 1000x1000 下占多少内存？”
    回答方向：int 是 4字节，100万个格子就是 4MB。其实不大。但如果为了极致优化，可以用 vector<uint8_t> 甚至 std::vector<bool> (BitMap) 来压缩到几百 KB。

搜索效率：
    “地图变大后，A 的 OPEN 表（优先队列）会变得很大，插入删除变慢，怎么优化？”*
    回答方向：除了二叉堆，是否考虑过其他的堆结构？或者使用 JPS 跳点搜索来减少入队的节点数量？

缓存友好性 (Cache Friendly)：
    “vector<vector<int>> 是连续内存吗？”
    回答方向：不是。它是一堆指向小 vector 的指针。
    优化：应该用一维数组 vector<int> grid_(width * height) 来模拟二维数组，通过 index = y * width + x 来访问。这样内存是连续的，CPU 缓存命中率高，性能在 1000x1000 的地图上会有显著提升。（如果你能做到这一点，面试官会非常欣赏）
*/

// obstacleRatio: 障碍物比例 (0.0 - 1.0)，比如 0.2 表示 20% 是墙
void GridMap::CreateRandomMap(int w, int h, double obstacleRatio) {
    width_ = w;
    height_ = h;
    grid_.assign(height_, std::vector<int>(width_, 0));

    /*
    先造种子→再造引擎→再定分布→最后按概率生成随机墙体。
    */
   
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    // 1. 随机墙体
    for (int i = 0; i < height_; ++i) {
        for (int j = 0; j < width_; ++j) {
            if (dis(gen) < obstacleRatio) {
                grid_[i][j] = 1;
            }
        }
    }
    
    // 2. 确保起点 (0,0) 和 这一类关键位置不是墙，防止开局就死
    grid_[0][0] = 0; 
    
    // 3.  加上四周围墙
    for(int i=0; i<width_; ++i) {
        grid_[0][i]=1;          // 上墙
        grid_[height_-1][i]=1;  // 下墙
    }
    for(int i=0; i<height_; ++i) { 
        grid_[i][0]=1;          // 左墙
        grid_[i][width_-1]=1;   // 右墙
    }

    LOG_INFO("Random Map Created: %dx%d with ratio %.2f",width_, height_, obstacleRatio);
}

bool GridMap::IsObstacle(int x, int y) const {
    // 越界检查
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        return true; 
    }
    
    return grid_[y][x];
}

bool GridMap::IsObstacle(const agv::model::Point& p) const {
    return IsObstacle(p.x, p.y);
}

void GridMap::PrintMap() {
    std::cout << "=== MAP PREVIEW (" << width_ << "x" << height_ << ") ===" << std::endl;
    
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            if (grid_[y][x] == 1) std::cout << "▇ "; // 墙
            else std::cout << ". ";                 // 路
        }
        std::cout << std::endl;
    }
    std::cout << "===========================" << std::endl;
}