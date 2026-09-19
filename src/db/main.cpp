// ============================================================================
//  main.cpp —— 程序入口
//
//  两种使用方式:
//    1) 无参数: 进入终端可视化界面(面向教务/学生等非技术用户);
//    2) 子命令: 便于脚本化调用与自动化验收, 例如
//         ./build/scheduler init-db              重建演示数据
//         ./build/scheduler cycle-check          循环依赖检测(有环返回码=2)
//         ./build/scheduler topo                 打印拓扑序与分层
//         ./build/scheduler schedule             执行排课(落库)
//         ./build/scheduler schedule --dry-run   仅预演不落库
//         ./build/scheduler timetable 2024001    打印学生周课表
//         ./build/scheduler available 2024001    打印可选课程(含不可选原因)
//         ./build/scheduler enroll 2024001 CS204-01
//         ./build/scheduler benchmark 5          效率基准测试
// ============================================================================
#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "scheduler/config.hpp"
#include "scheduler/db/database.hpp"
#include "scheduler/services/benchmark_service.hpp"
#include "scheduler/services/curriculum_service.hpp"
#include "scheduler/services/enrollment_service.hpp"
#include "scheduler/services/query_service.hpp"
#include "scheduler/services/scheduling_service.hpp"
#include "scheduler/ui/app.hpp"
#include "scheduler/ui/theme.hpp"
#include "scheduler/ui/widgets.hpp"
#include "scheduler/util/str.hpp"

namespace {

using scheduler::ui::Table;

void print_help() {
    std::cout <<
            R"(学生课表排课系统 (C++17 / MySQL / DAG 拓扑排序)

用法:
  scheduler                          进入终端可视化界面
  scheduler init-db                  建库并导入演示数据(schema + seed)
  scheduler cycle-check              检测先修关系是否存在循环依赖
  scheduler topo                     打印拓扑序列与先修分层
  scheduler schedule [选项]          执行教务排课
        --semester <学期>            指定学期(默认取配置中的当前学期)
        --dry-run                    仅预演不落库
        --naive                      使用朴素策略(基准对照)
  scheduler timetable <学号> [学期]  打印学生周课表
  scheduler available <学号> [学期]  打印可选课程与不可选原因
  scheduler enroll <学号> <教学班>   学生选课
  scheduler drop   <学号> <教学班>   学生退课
  scheduler benchmark [轮数]         排课效率基准测试(含冲突检测次数对比)
  scheduler scaling [最大规模]       核心算法规模扫描(默认 30/60/90/120 个教学班)
  scheduler stats [学期]             学期统计概览
  scheduler --help                   显示本帮助
)";
}

std::vector<std::string> split_args(int argc, char** argv) {
    return std::vector<std::string>(argv + 1, argv + argc);
}

std::string option_value(const std::vector<std::string>& args, const std::string& name,
                         const std::string& fallback) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) return args[i + 1];
    }
    return fallback;
}

bool has_flag(const std::vector<std::string>& args, const std::string& name) {
    for (const std::string& item : args) {
        if (item == name) return true;
    }
    return false;
}

// 取第 index 个位置参数(跳过以 - 开头的选项)
std::string positional(const std::vector<std::string>& args, std::size_t index) {
    std::size_t seen = 0;
    for (const std::string& item : args) {
        if (!item.empty() && item[0] == '-') continue;
        if (seen == index) return item;
        ++seen;
    }
    return std::string();
}

std::vector<std::string> keys_of(const scheduler::graph::CourseDAG& dag,
                                 const std::vector<int>& ids) {
    std::vector<std::string> keys;
    keys.reserve(ids.size());
    for (int id : ids) keys.push_back(dag.key_of(id));
    return keys;
}

int command_init_db(scheduler::db::Database& database) {
    const scheduler::Config& config = scheduler::app_config();
    scheduler::db::Database::create_database_if_missing(config);
    const int schema = database.run_sql_file(config.sql_dir() + "/01_schema.sql");
    const int seed = database.run_sql_file(config.sql_dir() + "/02_seed_data.sql");
    std::cout << scheduler::ui::theme().ok("数据库初始化完成") << "\n";
    std::cout << "  建表语句: " << schema << " 条, 种子数据: " << seed << " 条\n";
    const auto counts = database.table_counts({"courses", "course_prerequisites",
                                               "teaching_classes", "students"});
    for (const auto& entry : counts) {
        std::cout << "  " << entry.first << ": " << entry.second << " 行\n";
    }
    return 0;
}

int command_cycle_check(scheduler::db::Database& database) {
    scheduler::service::CurriculumService curriculum(database);
    const auto check = curriculum.check_cycle();
    std::cout << "拓扑序列长度 = " << check.sorted_count << ", 课程总数 = " << check.total_count
              << "\n";
    if (check.acyclic) {
        std::cout << scheduler::ui::theme().ok("未检测到循环依赖, 教学计划合法") << "\n";
        return 0;
    }
    std::cout << scheduler::ui::theme().error("检测到循环依赖") << "\n";
    std::cout << "  环路路径: " << scheduler::str::join(check.cycle_path, " → ") << "\n";
    std::cout << "  受阻课程: " << scheduler::str::join(check.pending_codes, "、") << "\n";
    return 2;   // 非零返回码, 便于 CI/脚本判断
}

int command_topo(scheduler::db::Database& database) {
    scheduler::service::CurriculumService curriculum(database);
    std::map<int, scheduler::Course> course_index;
    const scheduler::graph::CourseDAG dag = curriculum.build_dag(&course_index);
    const auto topo = dag.topological_sort(scheduler::graph::Priority::LevelThenKey);
    if (topo.has_cycle()) {
        std::cout << scheduler::ui::theme().error_box(
                             "存在循环依赖",
                             {"拓扑序列长度 " + std::to_string(topo.sorted_count()) +
                                      " ≠ 课程总数 " + std::to_string(topo.total),
                              "环路: " +
                                      scheduler::str::join(keys_of(dag, topo.cycle_path), " → ")})
                  << "\n";
        return 2;
    }
    std::cout << "DAG: " << dag.vertex_count() << " 个顶点 / " << dag.edge_count() << " 条边\n";
    std::vector<std::vector<std::string>> levels;
    for (const auto& group : dag.level_groups()) levels.push_back(keys_of(dag, group));
    std::cout << "\n先修分层:\n" << scheduler::ui::level_flow(levels);

    Table table({{"序号", 5, true}, {"课程", 10}, {"名称", 20}, {"学分", 6, true},
                 {"层级", 6, true}, {"先修课程", 26}});
    for (std::size_t i = 0; i < topo.order.size(); ++i) {
        const int id = topo.order[i];
        const auto course_it = course_index.find(id);
        table.add_row({std::to_string(i + 1), dag.key_of(id),
                       course_it == course_index.end() ? "-" : course_it->second.name,
                       course_it == course_index.end()
                               ? "-"
                               : scheduler::str::number(course_it->second.credits, 1),
                       std::to_string(topo.levels.count(id) ? topo.levels.at(id) : 0),
                       scheduler::str::join(keys_of(dag, dag.predecessors(id)), "、")});
    }
    std::cout << "\n" << table.render() << "\n";
    return 0;
}

int command_schedule(scheduler::db::Database& database, const std::vector<std::string>& args) {
    const bool dry_run = has_flag(args, "--dry-run");
    const bool naive = has_flag(args, "--naive");
    scheduler::service::SchedulingService scheduling(database);
    scheduler::service::SchedulingService::Options options;
    options.semester = option_value(args, "--semester", scheduler::app_config().current_semester);
    options.persist = !dry_run;
    options.use_topology = !naive;
    options.level_priority = !naive;
    options.strategy_label = naive ? "class-order-linear-scan" : "kahn-topological-greedy";

    const auto report = scheduling.generate(options);
    std::cout << scheduler::ui::theme().ok("排课完成") << "\n";
    std::cout << "  学期: " << report.semester << "  算法: " << report.algorithm
              << "  模式: " << (dry_run ? "预演(不落库)" : "正式(已落库)") << "\n";
    std::cout << "  课程(顶点): " << report.total_courses
              << "  教学班: " << report.total_classes
              << "  需要课次: " << report.requested_sessions
              << "  成功课次: " << report.scheduled_sessions << "\n";
    std::cout << "  冲突检测次数: " << report.conflict_probes << "  冲突重试次数: " << report.retries
              << "  未排成功教学班: " << report.failed_classes << "\n";
    std::cout << "  耗时: " << scheduler::str::duration_ms(report.duration_ms)
              << "  成功率: " << scheduler::str::percent(report.success_rate(), 1) << "\n";
    if (!dry_run) {
        const auto validation = scheduling.validate_semester(options.semester);
        std::cout << "  排课自检: 教师冲突=" << validation.teacher_conflicts
                  << " 教室冲突=" << validation.room_conflicts
                  << " 班级冲突=" << validation.class_conflicts << "\n";
    }
    for (const auto& failure : report.failures) {
        std::cout << scheduler::ui::theme().warn(failure.class_code + ": " + failure.reason)
                  << "\n";
    }
    return report.failed_classes == 0 ? 0 : 3;
}

int command_timetable(scheduler::db::Database& database, const std::vector<std::string>& args) {
    const std::string student_no = positional(args, 0);
    if (student_no.empty()) {
        std::cerr << "用法: scheduler timetable <学号> [学期]\n";
        return 1;
    }
    const std::string semester = positional(args, 1).empty()
                                        ? scheduler::app_config().current_semester
                                        : positional(args, 1);
    scheduler::service::EnrollmentService enrollment(database);
    scheduler::repo::CatalogRepository catalog(database);
    const auto result = enrollment.timetable(student_no, semester);

    std::cout << result.student.name << " (" << result.student.no << ")  " << semester << "  已选 "
              << result.items.size() << " 门 / 共 " << scheduler::str::number(result.total_credits, 1)
              << " 学分\n";

    std::vector<std::string> row_labels;
    int periods = 5;
    for (const scheduler::TimeSlot& slot : catalog.time_slots()) {
        if (slot.day_of_week != 1) continue;   // 只用周一列推导节次与时间
        periods = std::max(periods, slot.period_no);
        row_labels.push_back("第 " + std::to_string(slot.period_no) + " 大节|" +
                             slot.start_time.substr(0, 5) + "-" + slot.end_time.substr(0, 5));
    }

    std::vector<std::vector<scheduler::ui::GridCell>> grid(
            static_cast<std::size_t>(periods), std::vector<scheduler::ui::GridCell>(5));
    for (const scheduler::TimetableItem& item : result.items) {
        if (item.day_of_week < 1 || item.day_of_week > 5) continue;
        if (item.period_no < 1 || item.period_no > periods) continue;
        scheduler::ui::GridCell& cell =
                grid[static_cast<std::size_t>(item.period_no - 1)]
                    [static_cast<std::size_t>(item.day_of_week - 1)];
        cell.title = item.course_name;
        cell.teacher = item.teacher_name;
        cell.room = item.building + " " + item.room_code;
        cell.tag = item.class_code;
        cell.empty = false;
    }
    std::cout << "\n" << scheduler::ui::render_weekly_grid(grid, row_labels) << "\n";
    for (const std::string& name : result.unscheduled) {
        std::cout << scheduler::ui::theme().warn("已选但尚未排课: " + name) << "\n";
    }
    return 0;
}

int command_available(scheduler::db::Database& database, const std::vector<std::string>& args) {
    const std::string student_no = positional(args, 0);
    if (student_no.empty()) {
        std::cerr << "用法: scheduler available <学号> [学期]\n";
        return 1;
    }
    const std::string semester = positional(args, 1).empty()
                                        ? scheduler::app_config().current_semester
                                        : positional(args, 1);
    scheduler::service::EnrollmentService enrollment(database);
    const auto options = enrollment.class_options(student_no, semester);
    std::size_t selectable = 0;
    for (const auto& option : options) {
        if (option.selectable) ++selectable;
    }
    std::cout << "学期 " << semester << "  教学班 " << options.size() << " 个, 当前可选 "
              << selectable << " 个\n\n";
    Table table({{"教学班", 12}, {"课程", 18}, {"学分", 6, true}, {"教师", 8},
                 {"上课时间", 24}, {"状态", 14}, {"说明", 26}});
    for (const auto& option : options) {
        const std::string note =
                option.missing_prerequisites.empty()
                        ? scheduler::str::join(option.conflicts, "、")
                        : "缺先修: " + scheduler::str::join(option.missing_prerequisites, "、");
        table.add_row({option.teaching_class.code, option.course.name,
                       scheduler::str::number(option.course.credits, 1), option.teacher.name,
                       option.slot_text, option.status, note});
    }
    std::cout << table.render() << "\n";
    return 0;
}

int command_enroll(scheduler::db::Database& database, const std::vector<std::string>& args,
                   bool drop) {
    const std::string student_no = positional(args, 0);
    const std::string class_code = positional(args, 1);
    if (student_no.empty() || class_code.empty()) {
        std::cerr << "用法: scheduler " << (drop ? "drop" : "enroll") << " <学号> <教学班编号>\n";
        return 1;
    }
    scheduler::service::EnrollmentService enrollment(database);
    try {
        if (drop) {
            const auto result = enrollment.drop(student_no, class_code);
            std::cout << scheduler::ui::theme().ok(result.message) << "\n";
        } else {
            const auto result = enrollment.enroll(student_no, class_code);
            std::cout << scheduler::ui::theme().ok(result.message) << "\n";
        }
        return 0;
    } catch (const scheduler::SchedulerError& error) {
        std::cout << scheduler::ui::theme().error(error.what()) << "\n";
        return 4;   // 业务规则拒绝(先修/时间冲突/学分/容量等)
    }
}

int command_benchmark(scheduler::db::Database& database, const std::vector<std::string>& args) {
    int rounds = 5;
    const std::string first = positional(args, 0);
    if (!first.empty()) {
        try {
            rounds = std::max(1, std::stoi(first));
        } catch (...) {
            rounds = 5;
        }
    }
    scheduler::service::BenchmarkService benchmark(database);
    const auto comparison = benchmark.run(scheduler::app_config().current_semester, rounds);
    std::cout << "学期 " << comparison.semester << "  教学班 " << comparison.class_count
              << " 个 / 课次 " << comparison.session_count << " 个, 每策略重复 " << rounds
              << " 轮\n\n";
    Table table({{"策略", 20}, {"核心算法耗时", 14, true}, {"全流程耗时", 12, true},
                 {"冲突检测次数", 14, true}, {"成功率", 9, true}});
    table.add_row({comparison.optimized.name,
                   scheduler::str::duration_us(comparison.optimized.average_algorithm_us),
                   scheduler::str::duration_ms(comparison.optimized.average_duration_ms),
                   scheduler::str::thousands(
                           static_cast<long long>(comparison.optimized.conflict_probes)),
                   scheduler::str::percent(comparison.optimized.success_rate, 1)});
    table.add_row({comparison.naive.name,
                   scheduler::str::duration_us(comparison.naive.average_algorithm_us),
                   scheduler::str::duration_ms(comparison.naive.average_duration_ms),
                   scheduler::str::thousands(
                           static_cast<long long>(comparison.naive.conflict_probes)),
                   scheduler::str::percent(comparison.naive.success_rate, 1)});
    std::cout << table.render() << "\n\n";
    std::cout << "  核心算法耗时下降: " << scheduler::str::percent(comparison.time_improvement, 1)
              << "\n";
    std::cout << "  冲突检测次数下降: " << scheduler::str::percent(comparison.probe_improvement, 1)
              << "\n";
    std::cout << "  全流程耗时下降: "
              << scheduler::str::percent(comparison.total_time_improvement, 1) << "\n";
    std::cout << "  零失败轮次: " << comparison.perfect_schedules << "/" << comparison.rounds
              << "\n";
    std::cout << "  人工排课估算(参数 " << comparison.manual_minutes_per_class << " 分钟/班): "
              << comparison.manual_total_minutes << " 分钟\n";
    return 0;
}

// 规模扫描: 只测核心算法, 观察"冲突检测结构"带来的复杂度差异如何随规模放大
int command_scaling(scheduler::db::Database& database, const std::vector<std::string>& args) {
    int max_scale = 120;
    const std::string first = positional(args, 0);
    if (!first.empty()) {
        try {
            max_scale = std::max(10, std::stoi(first));
        } catch (...) {
            max_scale = 120;
        }
    }
    std::vector<int> scales;
    for (int scale = 30; scale <= max_scale; scale += 30) scales.push_back(scale);
    scheduler::service::BenchmarkService benchmark(database);
    const auto report = benchmark.scaling(scheduler::app_config().current_semester, scales, 20);

    std::cout << "学期 " << report.semester << "  真实资源: " << report.room_count << " 间教室 / "
              << report.slot_count << " 个时段, 每档重复 " << report.repeats << " 轮"
              << "(仅统计核心算法, 不含数据库读写)\n\n";
    Table table({{"教学班规模", 11, true}, {"课次需求", 9, true}, {"策略A耗时", 12, true},
                 {"策略B耗时", 12, true}, {"耗时倍率", 9, true}, {"策略A检测", 10, true},
                 {"策略B检测", 10, true}, {"成功率", 12}});
    for (const auto& row : report.rows) {
        table.add_row(
                {std::to_string(row.class_count), std::to_string(row.sessions),
                 scheduler::str::duration_us(static_cast<long long>(row.optimized_us)),
                 scheduler::str::duration_us(static_cast<long long>(row.naive_us)),
                 scheduler::str::number(row.speedup, 2) + "×",
                 scheduler::str::thousands(static_cast<long long>(row.optimized_probes)),
                 scheduler::str::thousands(static_cast<long long>(row.naive_probes)),
                 "A " + std::to_string(row.optimized_scheduled) + " / B " +
                         std::to_string(row.naive_scheduled)});
    }
    std::cout << table.render() << "\n";
    return 0;
}

int command_stats(scheduler::db::Database& database, const std::vector<std::string>& args) {
    const std::string semester = positional(args, 0).empty()
                                        ? scheduler::app_config().current_semester
                                        : positional(args, 0);
    scheduler::service::SchedulingService scheduling(database);
    const auto overview = scheduling.overview(semester);
    std::cout << "学期 " << semester << "\n";
    std::cout << "  课程数: " << overview.course_count << "    教学班数: " << overview.class_count
              << "    已排课教学班: " << overview.scheduled_class_count << "\n";
    std::cout << "  排课记录: " << overview.entry_count << " 条    在读学生: "
              << overview.student_count << "    选课记录: " << overview.enrollment_count << " 条\n";
    std::cout << "  学期开设学分合计: " << scheduler::str::number(overview.total_credits, 1) << "\n";
    const auto validation = scheduling.validate_semester(semester);
    std::cout << "  冲突自检: 教师=" << validation.teacher_conflicts
              << " 教室=" << validation.room_conflicts
              << " 班级=" << validation.class_conflicts << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args = split_args(argc, argv);
    if (!args.empty() && (args[0] == "--help" || args[0] == "-h" || args[0] == "help")) {
        print_help();
        return 0;
    }
    try {
        scheduler::db::Database database(scheduler::app_config());
        if (args.empty()) {
            return scheduler::ui::run_interactive(database);   // 终端可视化界面
        }
        const std::string command = args[0];
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        if (command == "init-db") return command_init_db(database);
        if (command == "cycle-check") return command_cycle_check(database);
        if (command == "topo") return command_topo(database);
        if (command == "schedule") return command_schedule(database, rest);
        if (command == "timetable") return command_timetable(database, rest);
        if (command == "available") return command_available(database, rest);
        if (command == "enroll") return command_enroll(database, rest, false);
        if (command == "drop") return command_enroll(database, rest, true);
        if (command == "benchmark") return command_benchmark(database, rest);
        if (command == "scaling") return command_scaling(database, rest);
        if (command == "stats") return command_stats(database, rest);
        std::cerr << "未知子命令: " << command << "\n\n";
        print_help();
        return 1;
    } catch (const scheduler::CycleDetectedError& error) {
        std::cerr << scheduler::ui::theme().error(error.detail()) << "\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << scheduler::ui::theme().error(error.what()) << "\n";
        return 1;
    }
}

