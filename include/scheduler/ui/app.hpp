// ============================================================================
//  ui/app.hpp —— 终端交互式界面
//
//  面向两类用户组织菜单:
//    · 教务端: 一键排课、先修关系维护、全校课表、教师工作量、教室使用率、排课审计;
//    · 学生端: 登录后查课表 / 看可选课程 / 选课 / 退课 / 先修进度 / 历史成绩。
//  所有页面都会在"操作结果"处给出明确结论(成功/失败 + 原因 + 下一步建议),
//  非技术用户无需了解 SQL 或算法细节即可完成日常教务工作。
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "scheduler/db/database.hpp"
#include "scheduler/exceptions.hpp"
#include "scheduler/types.hpp"

namespace scheduler::ui {

class Application {
public:
    explicit Application(db::Database& database) : db_(database) {}

    int run();   // 进入主菜单循环, 返回进程退出码

private:
    // ---- 菜单 ----
    void main_menu();
    void menu_academic();
    void menu_curriculum();
    void menu_student_portal();
    void menu_benchmark();
    void menu_system();

    // ---- 教务端 ----
    void action_generate_schedule();
    void action_topology_plan();
    void action_cycle_check();
    void action_cycle_demo();
    void action_school_timetable();
    void action_teacher_load();
    void action_room_usage();
    void action_student_list();
    void action_run_history();

    // ---- 课程与先修关系 ----
    void action_course_list();
    void action_prereq_map();
    void action_add_prereq();
    void action_remove_prereq();

    // ---- 学生端 ----
    void student_portal();
    void student_menu(const Student& student);
    void show_my_timetable(const Student& student);
    void show_available_classes(const Student& student);
    void do_enroll(const Student& student);
    void do_drop(const Student& student);
    void show_prereq_progress(const Student& student);
    void show_score_history(const Student& student);

    // ---- 基准测试与系统 ----
    void action_benchmark();
    void action_scaling_benchmark();
    void action_system_info();
    void action_init_database();

    // ---- 工具 ----
    void header(const std::string& subtitle) const;
    void show_error(const std::exception& error) const;
    std::string ask_semester() const;
    static std::string describe_slots_text(const std::vector<ScheduleEntry>& schedule);

    db::Database& db_;
};

// 便捷入口(供 main.cpp 调用)
int run_interactive(db::Database& database);

}  // namespace scheduler::ui
