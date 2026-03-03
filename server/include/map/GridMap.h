#pragma once
#include <vector>
#include <string>
#include "model/AgvStructs.h"

class GridMap {
public:
    GridMap();
    ~GridMap() = default;

    // 加载地图文件
    bool LoadMap(const std::string& filename);
    // 生成默认地图
    void CreateDefaultMap();
    // 随机生成地图
    void CreateRandomMap(int w, int h, double obstackeRation);

    // 核心功能：判断某个点是否是障碍物
    bool IsObstacle(int x, int y) const;
    bool IsObstacle(const agv::model::Point& p) const;

    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }

    void PrintMap(); // 在控制台打印预览

    // 获取随机可通行点（用于动态任务生成）
    agv::model::Point GetRandomWalkablePoint() const;

    // 生成 N 个均匀分布的安全起点（用于 AGV 初始位置）
    std::vector<agv::model::Point> GenerateSafeSpawnPoints(int count) const;

private:
    int width_ = 0;
    int height_ = 0;
    // 0: 空地, 1: 障碍
    std::vector<std::vector<int>> grid_;
};