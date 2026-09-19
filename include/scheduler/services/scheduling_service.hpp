// ============================================================================
//  services/scheduling_service.hpp —— 教务排课引擎
//
//  一键排课的完整业务流(这是本系统的核心流程):
//      ① 读取课程与先修关系 -> 装配 DAG
//      ② Kahn 拓扑排序; 若 输出序列数 ≠ 课程总数 则判定存在循环依赖并报错终止
//      ③ 按拓扑序(先修层级优先)生成教学班排课优先级队列
//      ④ 逐个教学班分配"时段 + 教室":
//           硬约束: 教师同一时段唯一 / 教室同一时段唯一 / 同一教学班时段不重复 /
//                   教室容量 >= 预期人数 / 教师教学班数不超过工作量上限
//           软约束: 上午优先、避免同一天连排、容量贴合(不浪费大教室)
//      ⑤ 事务落库(排课结果 + 审计记录), 输出统计报表
//
//  同时提供"朴素策略"(按教学班编号顺序、逐条全量比对已有课表)作为效率基准,
//  两者在同一数据集上对比, 量化排课效率提升(见 docs/DESIGN.md 基准测试章节)。
// ============================================================================
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "scheduler/db/database.hpp"
#include "scheduler/graph/dag.hpp"
#include "scheduler/repositories.hpp"
#include "scheduler/types.hpp"

namespace scheduler::service {

class SchedulingService {
public:
    struct Options {
        std::string semester;            // 空 = 使用配置中的当前学期
        bool persist = true;             // false 时只计算不落库(基准测试/预演)
        bool use_topology = true;        // true: 拓扑序优先; false: 朴素顺序(基准对照)
        bool level_priority = true;      // 同入度时先修层级优先
        std::string strategy_label;      // 写入审计表的算法标识
    };

    struct Report {
        std::string semester;
        std::string algorithm;
        std::size_t total_courses = 0;
        std::size_t total_classes = 0;
        std::size_t requested_sessions = 0;   // 需要安排的课次总数(班级数 × 每周节次)
        std::size_t scheduled_sessions = 0;
        std::size_t failed_classes = 0;
        std::size_t retries = 0;              // 因冲突而重新尝试候选方案的次数
        std::size_t conflict_probes = 0;      // 冲突检测调用次数(算法效率指标)
        long long duration_ms = 0;            // 全流程耗时(总)
        long long load_us = 0;                // 数据装载(读库 + 建图)耗时
        long long algorithm_us = 0;           // 核心排课算法耗时(不含读写数据库)
        long long persist_us = 0;             // 落库(事务写入)耗时
        long long schedule_id = -1;           // 审计记录 id
        bool cycle_detected = false;
        std::string message;
        std::vector<std::string> order_codes;              // 排课使用的课程顺序
        std::vector<std::vector<std::string>> level_groups; // 先修分层
        std::vector<Placement> placements;
        struct Failure {
            std::string class_code;
            std::string reason;
        };
        std::vector<Failure> failures;

        double success_rate() const {
            if (total_classes == 0) return 0.0;
            return static_cast<double>(total_classes - failed_classes) /
                   static_cast<double>(total_classes);
        }
    };

    // 排课质量校验结果(用于"排完后自检"与文档/测试断言)
    struct ValidationResult {
        int teacher_conflicts = 0;
        int room_conflicts = 0;
        int class_conflicts = 0;
        int entry_count = 0;
        bool clean() const {
            return teacher_conflicts == 0 && room_conflicts == 0 && class_conflicts == 0;
        }
    };

    // ---- 纯算法层: 输入快照 -> 排课方案(不接触数据库) ----
    //  把算法与存储彻底解耦, 带来三个好处:
    //    1) 基准测试可以只测算法, 不受数据库 I/O 噪声干扰;
    //    2) 单元测试可以用构造出来的快照直接验证算法行为;
    //    3) 未来换存储(或做"排课方案预演")时算法部分无需改动。
    struct SchedulingInputs {
        std::map<int, Course> courses;
        std::map<int, Teacher> teachers;
        std::vector<Classroom> classrooms;
        std::vector<TimeSlot> time_slots;
        std::vector<TeachingClass> classes;      // 参与排课的教学班
        graph::TopologicalResult topo;           // 拓扑排序结果(含先修层级与序号)
    };

    struct PlacementOutcome {
        std::vector<Placement> placements;
        std::vector<Report::Failure> failures;
        std::size_t scheduled_sessions = 0;
        std::size_t failed_classes = 0;
        std::size_t retries = 0;
        std::size_t conflict_probes = 0;
        long long algorithm_us = 0;
    };

    // 核心排课算法(纯函数): 按 use_topology 选择"拓扑序优先"或"朴素顺序"策略
    static PlacementOutcome run_placement(const SchedulingInputs& inputs, bool use_topology);

    explicit SchedulingService(db::Database& database)
        : db_(database), catalog_(database), teaching_(database) {}

    // 执行排课(核心入口)
    Report generate(const Options& options);

    // 清空某学期的排课结果(重新排课前调用)
    int clear_semester(const std::string& semester);

    // 冲突自检: 直接从数据库读回排课结果并检查三类冲突
    ValidationResult validate_semester(const std::string& semester);

    // 学期概览统计(排课结果 + 选课情况)
    struct Overview {
        std::string semester;
        int course_count = 0;
        int class_count = 0;
        int scheduled_class_count = 0;
        int entry_count = 0;
        int student_count = 0;
        int enrollment_count = 0;
        double total_credits = 0.0;
    };
    Overview overview(const std::string& semester);

private:
    db::Database& db_;
    repo::CatalogRepository catalog_;
    repo::TeachingRepository teaching_;
};

}  // namespace scheduler::service
