#pragma once
#include <vector>
#include <string>
#include "model/AgvStructs.h"

class GridMap {
public:
    GridMap();
    /*default : 强制生成默认实现
    增强代码意图：明确表达「考虑过析构逻辑，这个类没有需要手动释放的资源（比如堆内存、文件句柄、网络连接），用编译器默认的析构就够了」，。
    如果不写析构（什么都不写），虽然会有编译器隐式默认析构，但别人可能会疑惑「是不是忘了写析构？是不是有隐藏的资源没释放？」，加 = default 直接消除这种歧义。
    如果是：~GridMap() {}，会覆盖默认析构，虽然逻辑上也会调用成员 / 基类析构，但会丢失「内联属性」「异常规格」等编译器优化，且可能影响后续继承的析构行为 :
        手动写 {} 的坏处：这会让编译器认为你定义了“用户提供的析构函数”。在某些 C++ 版本中，这会阻止编译器自动生成移动构造函数（Move Constructor）和移动赋值运算符（Move Assignment Operator），导致本可以高效移动 vector 的地方变成了昂贵的拷贝。
    */
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

    /*
    1. 语法层面的自动（隐式 inline）
        在 C++ 标准中，直接定义在类声明内部（头文件中）的成员函数，会被编译器自动视为隐式 inline
    2. 编译优化层面的自动（实际内联）
        虽然语法上它有了 inline 属性，但最终是否真的把函数调用展开成汇编指令（即消除函数调用开销），取决于编译器（如 GCC/Clang/MSVC）的优化策略。
        Debug 模式 (-O0)：通常不会内联。为了方便断点调试，编译器会保留函数调用跳转。
        Release 模式 (-O2 / -O3)：绝对会内联。
            成本计算：编译器会计算成本。调用一个函数的开销（压栈、跳转、弹栈）可能需要几条指令，而直接把 width_ 的值搬到寄存器（mov 指令）只需要 1 条指令。
            决策：内联不仅更快，而且代码体积更小。编译器没有任何理由拒绝内联它。
        .h文件内类外写实现必须加 inline：
            void MyClass::func() {...} X
            inline void MyClass::func() {...} √
    */
    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }

    void PrintMap(); // 在控制台打印预览

    // 获取随机可通行点（用于动态任务生成）
    agv::model::Point GetRandomWalkablePoint() const;

// private:
    // 解析文件内容的辅助函数
    // bool ParseFile(const std::string& content);
 
private:
    int width_ = 0;
    int height_ = 0;
    // 0: 空地, 1: 障碍
    std::vector<std::vector<int>> grid_;
};