// ============================================================================
//  services/scheduling_service.cpp —— 教务排课引擎实现
// ============================================================================
#include "scheduler/services/scheduling_service.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "scheduler/config.hpp"
#include "scheduler/exceptions.hpp"
#include "scheduler/rules.hpp"
#include "scheduler/services/curriculum_service.hpp"
#include "scheduler/util/str.hpp"

namespace scheduler::service {
namespace {

using rules::Slot;

// 教室类型偏好: 计算机类实践课程优先机房, 其余课程优先普通教室(软约束)
RoomType preferred_room_type(const Course& course) {
    const bool computer_intensive =
            course.dept == "计算机学院" &&
            (course.type == CourseType::Elective || course.code == "CS101" ||
             course.code == "CS203" || course.code == "CS308");
    return computer_intensive ? RoomType::Computer : RoomType::General;
}

// 朴素策略的冲突判定: 每次都在"已排课表"数组里线性扫描。
// 这正是教务人员手工排课时的工作方式(拿着课表逐条比对), 也是未优化实现的常见写法;
// 用它作为基准, 可以量化"拓扑序 + 冲突矩阵"带来的效率提升。
bool linear_scan_conflict(const std::vector<Placement>& committed, int teacher_id,
                          int classroom_id, const Slot& slot, std::size_t& probes) {
    for (const Placement& placed : committed) {
        ++probes;
        if (placed.day_of_week != slot.day_of_week || placed.period_no != slot.period_no) {
            continue;
        }
        if (placed.teacher_id == teacher_id || placed.classroom_id == classroom_id) {
            return true;
        }
    }
    return false;
}

}  // namespace

// ===========================================================================
//  排课主流程(教务"一键排课"对应的完整业务流程)
// ===========================================================================
SchedulingService::Report SchedulingService::generate(const Options& options) {
    const Config& config = app_config();
    const auto start_time = std::chrono::steady_clock::now();
    // 分段计时: 便于把"读库开销"与"核心算法开销"分开衡量(基准测试依赖该拆分)
    const auto elapsed_us = [&start_time]() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start_time)
                .count();
    };

    Report report;
    report.semester = options.semester.empty() ? config.current_semester : options.semester;
    report.algorithm = options.strategy_label.empty()
                               ? (options.use_topology ? "kahn-topological-greedy"
                                                       : "class-order-linear-scan")
                               : options.strategy_label;

    // ---------------- ① 装配先修关系 DAG ----------------
    CurriculumService curriculum(db_);
    std::map<int, Course> courses;
    const graph::CourseDAG dag = curriculum.build_dag(&courses);
    report.total_courses = courses.size();

    // ---------------- ② 拓扑排序 + 循环依赖校验 ----------------
    const graph::Priority priority =
            options.level_priority ? graph::Priority::LevelThenKey : graph::Priority::StableByKey;
    const graph::TopologicalResult topo = dag.topological_sort(priority);

    // ★ 核心判据: 拓扑序列长度 != 课程总数 说明存在循环依赖, 立即终止排课
    if (topo.has_cycle()) {
        CycleDetectedError::Info info;
        info.cycle_path = dag.keys_of(topo.cycle_path);
        info.pending_codes = dag.keys_of(topo.pending);
        info.sorted_count = topo.sorted_count();
        info.total_count = topo.total;

        repo::SchedulingRunRecord audit;
        audit.semester = report.semester;
        audit.algorithm = report.algorithm;
        audit.started_at = str::now_datetime();
        audit.finished_at = audit.started_at;
        audit.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - start_time)
                                    .count();
        audit.total_courses = static_cast<int>(info.total_count);
        audit.status = "cycle_detected";
        audit.message = "拓扑序列 " + std::to_string(info.sorted_count) + " ≠ 课程总数 " +
                        std::to_string(info.total_count) + ", 环路: " +
                        str::join(info.cycle_path, " → ");
        teaching_.insert_run(audit);   // 失败也留痕, 便于交接排查

        throw CycleDetectedError("排课中止: 课程先修关系存在循环依赖, 无法确定先修顺序",
                                 std::move(info));
    }

    report.order_codes = dag.keys_of(topo.order);
    for (const std::vector<int>& group : dag.level_groups()) {
        report.level_groups.push_back(dag.keys_of(group));
    }

    // ---------------- ③ 汇总排课资源与教学班 ----------------
    const std::vector<Teacher> teacher_list = catalog_.teachers();
    std::map<int, Teacher> teacher_index;
    for (const Teacher& teacher : teacher_list) teacher_index[teacher.id] = teacher;

    const std::vector<Classroom> classrooms = catalog_.classrooms();
    const std::vector<TimeSlot> time_slots = catalog_.time_slots();
    std::vector<TeachingClass> classes = teaching_.classes_of_semester(report.semester);
    report.total_classes = classes.size();
    for (const TeachingClass& item : classes) {
        const auto course_it = courses.find(item.course_id);
        if (course_it != courses.end()) {
            report.requested_sessions += static_cast<std::size_t>(course_it->second.hours_per_week);
        }
    }
    if (classes.empty()) {
        report.message = "该学期没有教学班, 无需排课";
        report.load_us = elapsed_us();
        report.algorithm_us = 0;
        report.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start_time)
                                     .count();
        return report;
    }
    // 排课顺序: 拓扑序(先修课优先占用稀缺资源) 或 朴素的教学班编号顺序
    if (options.use_topology) {
        std::sort(classes.begin(), classes.end(),
                  [&topo](const TeachingClass& lhs, const TeachingClass& rhs) {
                      const int lhs_pos = topo.position_of(lhs.course_id);
                      const int rhs_pos = topo.position_of(rhs.course_id);
                      if (lhs_pos != rhs_pos) return lhs_pos < rhs_pos;
                      return lhs.code < rhs.code;
                  });
    } else {
        std::sort(classes.begin(), classes.end(),
                  [](const TeachingClass& lhs, const TeachingClass& rhs) {
                      return lhs.code < rhs.code;
                  });
    }
    report.message = options.use_topology ? "按拓扑序(先修层级优先)排课"
                                          : "按教学班编号顺序排课(基准策略)";
    report.load_us = elapsed_us();   // 建图 + 读库到此结束, 之后的耗时属于核心算法

    // ---------------- ④ 调用核心排课算法(纯内存计算) ----------------
    SchedulingInputs inputs;
    inputs.courses = std::move(courses);
    inputs.teachers = std::move(teacher_index);
    inputs.classrooms = classrooms;
    inputs.time_slots = time_slots;
    inputs.classes = std::move(classes);
    inputs.topo = topo;

    const PlacementOutcome outcome = run_placement(inputs, options.use_topology);
    report.placements = outcome.placements;
    report.failures = outcome.failures;
    report.scheduled_sessions = outcome.scheduled_sessions;
    report.failed_classes = outcome.failed_classes;
    report.retries = outcome.retries;
    report.conflict_probes = outcome.conflict_probes;
    report.algorithm_us = outcome.algorithm_us;
    const std::vector<Placement>& new_placements = report.placements;

    // ---------------- ⑤ 事务落库(排课结果 + 审计记录) ----------------
    if (options.persist) {
        std::vector<ScheduleEntry> entries;
        entries.reserve(new_placements.size());
        for (const Placement& placement : new_placements) {
            ScheduleEntry entry;
            entry.teaching_class_id = placement.teaching_class_id;
            entry.course_id = placement.course_id;
            entry.teacher_id = placement.teacher_id;
            entry.classroom_id = placement.classroom_id;
            entry.semester = report.semester;
            entry.day_of_week = placement.day_of_week;
            entry.period_no = placement.period_no;
            entries.push_back(entry);
        }

        repo::SchedulingRunRecord audit;
        audit.semester = report.semester;
        audit.algorithm = report.algorithm;
        audit.started_at = str::now_datetime();
        audit.total_courses = static_cast<int>(report.total_courses);
        audit.total_classes = static_cast<int>(report.total_classes);
        audit.scheduled_entries = static_cast<int>(entries.size());
        audit.failed_classes = static_cast<int>(report.failed_classes);
        audit.status = report.failed_classes == 0 ? "success"
                                                   : (entries.empty() ? "failed" : "partial");
        audit.message = report.message;

        const int replaced =
                static_cast<int>(teaching_.schedule_of_semester(report.semester).size());
        // 同一个事务内完成"清空旧结果 + 写入新结果 + 记录审计", 保证数据不会半新半旧
        db_.transaction([&](db::Database& transaction) {
            repo::TeachingRepository transaction_teaching(transaction);
            transaction_teaching.delete_schedule(report.semester);
            transaction_teaching.insert_schedule_entries(transaction, entries);
            audit.finished_at = str::now_datetime();
            audit.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - start_time)
                                        .count();
            audit.id = transaction_teaching.insert_run(audit);
        });
        report.schedule_id = audit.id;
        if (replaced > 0) {
            report.message += "; 已覆盖原有 " + std::to_string(replaced) + " 条排课记录";
        }
    }

    report.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start_time)
                                 .count();
    report.persist_us = elapsed_us() - report.load_us - report.algorithm_us;
    if (report.failed_classes == 0) {
        report.message += "; 全部 " + std::to_string(report.total_classes) + " 个教学班排课成功";
    } else {
        report.message += "; " + std::to_string(report.failed_classes) + " 个教学班需人工处理";
    }
    return report;
}

int SchedulingService::clear_semester(const std::string& semester) {
    return teaching_.delete_schedule(semester);
}

// 排课结果自检: 从数据库读回结果, 独立检查三类冲突是否真的为 0
SchedulingService::ValidationResult SchedulingService::validate_semester(
        const std::string& semester) {
    const std::vector<ScheduleEntry> entries = teaching_.schedule_of_semester(semester);
    ValidationResult result;
    result.entry_count = static_cast<int>(entries.size());

    // 数据库层已有唯一索引兜底, 这里再做一次独立校验, 用于自动化验收与排查
    std::set<std::pair<int, Slot>> teacher_slots;
    std::set<std::pair<int, Slot>> room_slots;
    std::set<std::pair<int, Slot>> class_slots;
    for (const ScheduleEntry& entry : entries) {
        const Slot slot{entry.day_of_week, entry.period_no};
        if (!teacher_slots.insert({entry.teacher_id, slot}).second) ++result.teacher_conflicts;
        if (!room_slots.insert({entry.classroom_id, slot}).second) ++result.room_conflicts;
        if (!class_slots.insert({entry.teaching_class_id, slot}).second) ++result.class_conflicts;
    }
    return result;
}

// ===========================================================================
//  核心排课算法(纯函数, 不接触数据库)
//
//  输入: 内存快照(课程 / 教师 / 教室 / 节次 / 教学班 / 拓扑排序结果)
//  输出: 时段+教室分配方案、失败明细、冲突检测次数、算法耗时
//
//  策略差异(use_topology):
//    true  —— 按拓扑序取教学班(先修课优先占用稀缺时段), 冲突判定使用集合查询;
//    false —— 按教学班编号顺序取班, 冲突判定对"已排课表"逐条线性扫描(基准对照)。
//  两种策略的硬约束完全一致, 因此成败结果可比, 差异只体现在检测效率上。
// ===========================================================================
SchedulingService::PlacementOutcome SchedulingService::run_placement(
        const SchedulingInputs& inputs, bool use_topology) {
    const auto algorithm_start = std::chrono::steady_clock::now();
    PlacementOutcome outcome;

    // 排课顺序
    std::vector<TeachingClass> queue = inputs.classes;
    if (use_topology) {
        std::sort(queue.begin(), queue.end(),
                  [&inputs](const TeachingClass& lhs, const TeachingClass& rhs) {
                      const int lhs_pos = inputs.topo.position_of(lhs.course_id);
                      const int rhs_pos = inputs.topo.position_of(rhs.course_id);
                      if (lhs_pos != rhs_pos) return lhs_pos < rhs_pos;   // 拓扑序: 先修在前
                      return lhs.code < rhs.code;
                  });
    } else {
        std::sort(queue.begin(), queue.end(),
                  [](const TeachingClass& lhs, const TeachingClass& rhs) {
                      return lhs.code < rhs.code;
                  });
    }

    rules::ConflictMatrix matrix;            // 优化策略: 集合查询, O(log n) 判定冲突
    std::vector<Placement> committed;        // 已确定的方案(朴素策略靠它线性扫描)
    std::map<int, int> teacher_assigned;     // 教师已承担的教学班数
    std::size_t naive_probes = 0;            // 朴素策略的冲突检测次数

    for (const TeachingClass& teaching_class : queue) {
        const auto course_it = inputs.courses.find(teaching_class.course_id);
        const auto teacher_it = inputs.teachers.find(teaching_class.teacher_id);
        if (course_it == inputs.courses.end() || teacher_it == inputs.teachers.end()) {
            ++outcome.failed_classes;
            outcome.failures.push_back({teaching_class.code, "课程或教师数据缺失"});
            continue;
        }
        const Course& course = course_it->second;
        const Teacher& teacher = teacher_it->second;

        // 硬约束: 教师工作量上限(单学期最多承担 max_classes 个教学班)
        if (teacher_assigned[teacher.id] >= teacher.max_classes) {
            ++outcome.failed_classes;
            outcome.failures.push_back(
                    {teaching_class.code, "教师 " + teacher.name + " 已达工作量上限(" +
                                                  std::to_string(teacher.max_classes) +
                                                  " 个教学班), 需教务调剂"});
            continue;
        }

        // 教室候选只与"预期人数 + 课程类型"有关, 因此在时段循环外只计算一次
        const std::vector<const Classroom*> ranked_rooms = rules::rank_classrooms(
                inputs.classrooms, std::max(teaching_class.expected_students, 1),
                preferred_room_type(course));
        std::vector<Placement> class_placements;
        std::set<Slot> class_slots;   // 本班已占时段, 保证同一班级不重复占用

        for (int session = 0; session < course.hours_per_week; ++session) {
            bool placed = false;
            const std::vector<Slot> candidate_slots =
                    rules::order_candidate_slots(inputs.time_slots, class_slots);
            for (const Slot& slot : candidate_slots) {
                // 先判教师(冲突则整个时段跳过), 再逐个教室尝试 —— 减少无效探测
                const bool teacher_busy =
                        use_topology ? matrix.teacher_conflict(teacher.id, slot)
                                     : linear_scan_conflict(committed, teacher.id, -1, slot,
                                                            naive_probes);
                if (teacher_busy) {
                    ++outcome.retries;
                    continue;
                }
                for (const Classroom* room : ranked_rooms) {
                    const bool room_busy =
                            use_topology ? matrix.room_conflict(room->id, slot)
                                         : linear_scan_conflict(committed, -1, room->id, slot,
                                                                naive_probes);
                    if (room_busy) {
                        ++outcome.retries;   // 冲突 -> 换下一个候选(重试)
                        continue;
                    }
                    Placement placement;
                    placement.teaching_class_id = teaching_class.id;
                    placement.course_id = course.id;
                    placement.teacher_id = teacher.id;
                    placement.classroom_id = room->id;
                    placement.day_of_week = slot.day_of_week;
                    placement.period_no = slot.period_no;
                    class_placements.push_back(placement);
                    class_slots.insert(slot);
                    placed = true;
                    break;
                }
                if (placed) break;
            }
            if (!placed) break;   // 本次课找不到无冲突组合 -> 该班交由人工处理
        }

        if (class_placements.size() == static_cast<std::size_t>(course.hours_per_week)) {
            // 全部课次都排成功才提交占用, 避免出现"排了一半"的教学班
            for (const Placement& placement : class_placements) {
                const Slot slot{placement.day_of_week, placement.period_no};
                matrix.occupy_teacher(placement.teacher_id, slot);
                matrix.occupy_room(placement.classroom_id, slot);
                committed.push_back(placement);
                outcome.placements.push_back(placement);
            }
            teacher_assigned[teacher.id] += 1;
            outcome.scheduled_sessions += class_placements.size();
        } else {
            ++outcome.failed_classes;
            outcome.failures.push_back(
                    {teaching_class.code, "时段/教室资源不足: 无法为每周 " +
                                                  std::to_string(course.hours_per_week) +
                                                  " 次课找到无冲突组合"});
        }
    }

    outcome.conflict_probes = use_topology ? matrix.probe_count() : naive_probes;
    outcome.algorithm_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now() - algorithm_start)
                                   .count();
    return outcome;
}

// 学期概览统计(排课结果 + 选课情况)
SchedulingService::Overview SchedulingService::overview(const std::string& semester) {
    Overview overview;
    overview.semester = semester;
    const auto classes = teaching_.classes_of_semester(semester);
    overview.class_count = static_cast<int>(classes.size());
    overview.scheduled_class_count = static_cast<int>(teaching_.scheduled_class_ids(semester).size());
    overview.entry_count = static_cast<int>(teaching_.schedule_of_semester(semester).size());

    const db::ResultSet course_count = db_.query(
            "SELECT COUNT(DISTINCT tc.course_id) AS total FROM teaching_classes tc "
            "WHERE tc.semester_code = ?",
            {semester});
    overview.course_count = course_count.empty() ? 0 : course_count.first().get_int("total");

    const db::ResultSet student_count = db_.query(
            "SELECT COUNT(*) AS total FROM students WHERE status = 'active'");
    overview.student_count = student_count.empty() ? 0 : student_count.first().get_int("total");

    const db::ResultSet enrollment = db_.query(
            "SELECT COUNT(*) AS total FROM enrollments WHERE semester_code = ? "
            "AND status IN ('enrolled','completed')",
            {semester});
    overview.enrollment_count = enrollment.empty() ? 0 : enrollment.first().get_int("total");

    const db::ResultSet credits = db_.query(
            "SELECT COALESCE(SUM(c.credits), 0) AS total FROM teaching_classes tc "
            "JOIN courses c ON c.id = tc.course_id WHERE tc.semester_code = ?",
            {semester});
    overview.total_credits = credits.empty() ? 0.0 : credits.first().get_double("total");
    return overview;
}

}  // namespace scheduler::service

