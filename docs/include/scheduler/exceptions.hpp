// ============================================================================
//  exceptions.hpp —— 业务异常体系
//  设计原则:
//    1) 所有可预期的业务失败(先修未通过/时间冲突/检测到课程环)都以异常表达,
//       调用方必须显式处理, 避免"静默失败";
//    2) 异常对象携带结构化数据(冲突明细、缺失先修、环路路径),
//       终端界面可直接渲染, 不需要再查库拼装提示;
//    3) CycleDetectedError 是本项目最关键的异常: 它对应"拓扑序列长度 !=
//       课程总数"这一判据, 详见 graph/dag.cpp 的 Kahn 算法实现。
// ============================================================================
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace scheduler {

class SchedulerError : public std::runtime_error {
public:
    explicit SchedulerError(const std::string& message) : std::runtime_error(message) {}
};

// 数据库连接或 SQL 执行失败
class DatabaseError : public SchedulerError {
public:
    explicit DatabaseError(const std::string& message) : SchedulerError(message) {}
};

// 实体不存在
class NotFoundError : public SchedulerError {
public:
    explicit NotFoundError(const std::string& message) : SchedulerError(message) {}
};

// ---------------------------------------------------------------------------
// 循环依赖(课程环): 拓扑排序输出序列数 != 课程总数时抛出的异常
// ---------------------------------------------------------------------------
class CycleDetectedError : public SchedulerError {
public:
    struct Info {
        std::vector<std::string> cycle_path;      // 具体环路, 如 CS201 -> CS204 -> CS201
        std::vector<std::string> pending_codes;   // 所有无法排序(入度无法归零)的课程
        std::size_t sorted_count = 0;             // 拓扑序列长度
        std::size_t total_count = 0;              // 课程总数
    };

    explicit CycleDetectedError(const std::string& message, Info info)
        : SchedulerError(message), info_(std::move(info)) {}

    const Info& info() const { return info_; }

    // 终端可直接打印的多行诊断文本
    std::string detail() const {
        std::string text = what();
        text += "\n判据: 拓扑序列长度 " + std::to_string(info_.sorted_count) +
                " ≠ 课程总数 " + std::to_string(info_.total_count) + " (相差 " +
                std::to_string(info_.total_count - info_.sorted_count) + " 门课程无法排序)";
        if (!info_.cycle_path.empty()) {
            text += "\n环路路径: ";
            for (std::size_t i = 0; i < info_.cycle_path.size(); ++i) {
                text += (i ? " → " : "") + info_.cycle_path[i];
            }
        }
        if (!info_.pending_codes.empty()) {
            text += "\n受阻课程(" + std::to_string(info_.pending_codes.size()) + " 门): ";
            for (std::size_t i = 0; i < info_.pending_codes.size(); ++i) {
                text += (i ? ", " : "") + info_.pending_codes[i];
            }
        }
        return text;
    }

private:
    Info info_;
};

// 先修课程未通过
class PrerequisiteError : public SchedulerError {
public:
    PrerequisiteError(const std::string& message, std::vector<std::string> missing)
        : SchedulerError(message), missing_(std::move(missing)) {}

    const std::vector<std::string>& missing() const { return missing_; }

private:
    std::vector<std::string> missing_;
};

// 选课时间冲突
class TimeConflictError : public SchedulerError {
public:
    TimeConflictError(const std::string& message, std::vector<std::string> conflicts)
        : SchedulerError(message), conflicts_(std::move(conflicts)) {}

    const std::vector<std::string>& conflicts() const { return conflicts_; }

private:
    std::vector<std::string> conflicts_;
};

// 超出学期学分上限
class CreditLimitError : public SchedulerError {
public:
    CreditLimitError(const std::string& message, double current, double limit)
        : SchedulerError(message), current_(current), limit_(limit) {}

    double current() const { return current_; }
    double limit() const { return limit_; }

private:
    double current_;
    double limit_;
};

// 教学班容量已满 / 未开放选课 / 尚未排课
class CapacityError : public SchedulerError {
public:
    explicit CapacityError(const std::string& message) : SchedulerError(message) {}
};

class EnrollmentClosedError : public SchedulerError {
public:
    explicit EnrollmentClosedError(const std::string& message) : SchedulerError(message) {}
};

class NotScheduledError : public SchedulerError {
public:
    explicit NotScheduledError(const std::string& message) : SchedulerError(message) {}
};

// 排课时找不到任何无冲突的"时段+教室"组合
class ConflictFreeSlotNotFound : public SchedulerError {
public:
    explicit ConflictFreeSlotNotFound(const std::string& message) : SchedulerError(message) {}
};

}  // namespace scheduler
