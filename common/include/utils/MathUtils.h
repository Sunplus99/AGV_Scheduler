#pragma once
#include "model/AgvStructs.h"
#include "cmath"


// 曼哈顿距离计算 
inline int CalMhtDis(const agv::model::Point& p1, const agv::model::Point& p2) {
    return std::abs(p1.x - p2.x) + std::abs(p1.y - p2.y);
}