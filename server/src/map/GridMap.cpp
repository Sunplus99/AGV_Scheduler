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

    // 2. 加上四周围墙
    for(int i=0; i<width_; ++i) {
        grid_[0][i]=1;          // 上墙
        grid_[height_-1][i]=1;  // 下墙
    }
    for(int i=0; i<height_; ++i) {
        grid_[i][0]=1;          // 左墙
        grid_[i][width_-1]=1;   // 右墙
    }

    // 3. 预留安全起点区域（网格分布）
    // 将地图分成 4x4 的网格，每个网格中心作为潜在的安全起点
    int gridSize = 4;
    int cellWidth = (width_ - 2) / gridSize;   // 减去边界墙
    int cellHeight = (height_ - 2) / gridSize;

    for (int gy = 0; gy < gridSize; ++gy) {
        for (int gx = 0; gx < gridSize; ++gx) {
            int cx = 1 + gx * cellWidth + cellWidth / 2;
            int cy = 1 + gy * cellHeight + cellHeight / 2;

            // 确保中心点及其周围 3x3 区域可通行
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int x = cx + dx;
                    int y = cy + dy;
                    if (x > 0 && x < width_ - 1 && y > 0 && y < height_ - 1) {
                        grid_[y][x] = 0;
                    }
                }
            }
        }
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
// 获取随机可通行点（用于动态任务生成）
agv::model::Point GridMap::GetRandomWalkablePoint() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());

    // 最多尝试 1000 次，避免死循环
    for (int attempt = 0; attempt < 1000; ++attempt) {
        std::uniform_int_distribution<> disX(1, width_ - 2);  // 避开边界墙
        std::uniform_int_distribution<> disY(1, height_ - 2);

        int x = disX(gen);
        int y = disY(gen);

        if (!IsObstacle(x, y)) {
            return {x, y};
        }
    }

    // 如果 1000 次都没找到，返回一个安全的默认点
    return {1, 1};
}

// 生成 N 个均匀分布的安全起点
std::vector<agv::model::Point> GridMap::GenerateSafeSpawnPoints(int count) const {
    std::vector<agv::model::Point> points;

    // 将地图分成网格，每个网格中心作为起点
    int gridSize = static_cast<int>(std::ceil(std::sqrt(count)));
    int cellWidth = (width_ - 2) / gridSize;
    int cellHeight = (height_ - 2) / gridSize;

    int generated = 0;
    for (int gy = 0; gy < gridSize && generated < count; ++gy) {
        for (int gx = 0; gx < gridSize && generated < count; ++gx) {
            int cx = 1 + gx * cellWidth + cellWidth / 2;
            int cy = 1 + gy * cellHeight + cellHeight / 2;

            // 确保在地图范围内且可通行
            if (cx > 0 && cx < width_ - 1 && cy > 0 && cy < height_ - 1 && !IsObstacle(cx, cy)) {
                points.push_back({cx, cy});
                generated++;
            }
        }
    }

    return points;
}
