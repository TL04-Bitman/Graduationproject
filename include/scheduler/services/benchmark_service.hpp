// ============================================================================
//  services/benchmark_service.hpp —— 排课效率基准测试
//
//  为什么需要它:
//      "效率提升"不能只靠口头宣称。本模块在同一份数据上运行两种策略并实测耗时:
//        策略 A(本系统): 拓扑序 + 先修层级优先 + 冲突矩阵(哈希集合)预判
//        策略 B(对照/朴素): 按教学班编号顺序, 每次尝试都全量扫描已有课表判断冲突
//      报告输出两者的耗时、冲突检测次数、重试次数与提升比例, 结果可复现。
//
//  另附"人工排课估算模型": 班次数 × 单班人工耗时(默认 3 分钟, 可在 .env 配置),
//  该模型只用于把机器耗时换算成教务人工排课的等效工作量, 参数取值写在报告里,
//  便于审阅者自行调整与复核。
// ============================================================================
#pragma once

#include <cstddef>
#include <string>

#include "scheduler/db/database.hpp"

namespace scheduler::service {

class BenchmarkService {
public:
    struct StrategyResult {
        std::string name;
        std::string description;
        long long average_duration_ms = 0;      // 全流程平均耗时(含读库/落库)
        long long average_algorithm_us = 0;     // 核心算法平均耗时(不含数据库读写)
        std::size_t conflict_probes = 0;
        std::size_t retries = 0;
        std::size_t scheduled_sessions = 0;
        std::size_t failed_classes = 0;
        double success_rate = 0.0;
    };

    struct Comparison {
        std::string semester;
        int rounds = 1;
        std::size_t class_count = 0;
        std::size_t session_count = 0;
        StrategyResult optimized;
        StrategyResult naive;
        double time_improvement = 0.0;       // 核心算法耗时下降比例(基准结论)
        double probe_improvement = 0.0;      // 冲突检测次数下降比例
        double total_time_improvement = 0.0; // 全流程耗时下降比例
        double manual_minutes_per_class = 0.0;
        double manual_total_minutes = 0.0;   // 人工排课估算总耗时(分钟)
        double manual_speedup = 0.0;         // 倍速 = 人工估算耗时 / 系统实测耗时
        std::size_t perfect_schedules = 0;   // 无失败班级的轮次数(策略 A 的稳定性)
    };

    explicit BenchmarkService(db::Database& database) : db_(database) {}

    Comparison run(std::string semester = "", int rounds = 1);

    // -----------------------------------------------------------------------
    //  规模扫描基准: 在同一批真实教学资源上, 用合成的教学班规模 N 反复测两种策略,
    //  只统计"核心算法"耗时(不含任何数据库读写), 用于观察复杂度带来的差距增长。
    //  说明: 合成教学班时会放宽教师工作量上限, 以便在同样资源下放大规模。
    // -----------------------------------------------------------------------
    struct ScaleRow {
        int class_count = 0;
        std::size_t sessions = 0;
        double optimized_us = 0.0;
        double naive_us = 0.0;
        double speedup = 0.0;               // naive_us / optimized_us
        std::size_t optimized_probes = 0;
        std::size_t naive_probes = 0;
        std::size_t optimized_scheduled = 0;
        std::size_t naive_scheduled = 0;
        std::size_t optimized_failed = 0;
        std::size_t naive_failed = 0;
    };
    struct ScalingReport {
        std::string semester;
        int repeats = 0;
        int room_count = 0;
        int slot_count = 0;
        std::vector<ScaleRow> rows;
    };
    ScalingReport scaling(std::string semester = "", std::vector<int> scales = {30, 60, 90, 120},
                          int repeats = 20);

private:
    db::Database& db_;
};

}  // namespace scheduler::service
