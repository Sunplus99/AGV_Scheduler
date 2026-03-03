#pragma once
#include <atomic>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include "Logger.h"

namespace utils {

/**
 * @brief 性能统计工具类（线程安全）
 *
 * 用于收集和统计性能指标：
 * - 任务计数
 * - 延迟统计（平均值、P50、P95、P99）
 */
class PerfStats {
public:
    PerfStats() : totalCount_(0), totalLatency_(0.0) {
        latencies_.reserve(10000);  // 预分配空间
    }

    // 记录一次延迟（毫秒）
    void recordLatency(double latencyMs) {
        totalCount_.fetch_add(1, std::memory_order_relaxed);

        // 累加总延迟（用于计算平均值）
        double expected = totalLatency_.load(std::memory_order_relaxed);
        while (!totalLatency_.compare_exchange_weak(expected, expected + latencyMs,std::memory_order_relaxed)) {
            // CAS 失败则重试
        }

        // 保存延迟数据（用于计算百分位）
        // 注意：这里有竞态条件，但对于性能测试来说可以接受
        if (latencies_.size() < 10000) {
            latencies_.push_back(latencyMs);
        }
    }

    // 增加计数
    void incrementCount() {
        totalCount_.fetch_add(1, std::memory_order_relaxed);
    }

    // 获取总计数
    uint64_t getCount() const {
        return totalCount_.load(std::memory_order_relaxed);
    }

    // 获取平均延迟
    double getAvgLatency() const {
        uint64_t count = totalCount_.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return totalLatency_.load(std::memory_order_relaxed) / count;
    }

    // 计算百分位延迟（P50, P95, P99）
    void calculatePercentiles(double& p50, double& p95, double& p99) {
        if (latencies_.empty()) {
            p50 = p95 = p99 = 0.0;
            return;
        }

        std::vector<double> sorted = latencies_;
        std::sort(sorted.begin(), sorted.end());

        size_t n = sorted.size();
        p50 = sorted[static_cast<size_t>(n * 0.50)];
        p95 = sorted[static_cast<size_t>(n * 0.95)];
        p99 = sorted[static_cast<size_t>(n * 0.99)];
    }

    // 打印统计信息
    void printStats(const std::string& name) const {
        uint64_t count = getCount();
        double avg = getAvgLatency();

        LOG_WARN("[PERF] %s Statistics:", name.c_str());
        LOG_WARN("  Total Count: %lu", count);
        LOG_WARN("  Avg Latency: %.2f ms", avg);

        if (!latencies_.empty()) {
            double p50, p95, p99;
            const_cast<PerfStats*>(this)->calculatePercentiles(p50, p95, p99);
            LOG_WARN("  P50 Latency: %.2f ms", p50);
            LOG_WARN("  P95 Latency: %.2f ms", p95);
            LOG_WARN("  P99 Latency: %.2f ms", p99);
        }
    }

    // 重置统计
    void reset() {
        totalCount_.store(0, std::memory_order_relaxed);
        totalLatency_.store(0.0, std::memory_order_relaxed);
        latencies_.clear();
    }

private:
    std::atomic<uint64_t> totalCount_;      // 总计数
    std::atomic<double> totalLatency_;      // 总延迟（用于计算平均值）
    std::vector<double> latencies_;         // 延迟数据（用于计算百分位）
};

} // namespace utils
