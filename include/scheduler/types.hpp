// ============================================================================
//  types.hpp —— 领域实体与枚举定义
//  说明: 全项目共享的数据结构都收敛在这里, 与数据库表结构一一对应,
//        便于"表 -> 结构体 -> 业务"三层对照阅读。
// ============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace scheduler {

// ---------------------------------------------------------------- 枚举与标签
enum class CourseType { Required, Elective, General };
enum class RoomType { General, Computer, Lab };
enum class EnrollmentStatus { Enrolled, Completed, Failed, Dropped };

// 数据库枚举值 <-> 内存枚举(数据库统一使用英文码, 界面展示中文, 避免编码耦合)
std::string to_db_string(CourseType type);
std::string to_db_string(RoomType type);
std::string to_db_string(EnrollmentStatus status);
CourseType course_type_from_db(const std::string& value);
RoomType room_type_from_db(const std::string& value);
EnrollmentStatus enrollment_status_from_db(const std::string& value);

// 界面展示用的中文标签
std::string label(CourseType type);
std::string label(RoomType type);
std::string label(EnrollmentStatus status);
std::string label(int day_of_week);   // 1 -> 星期一

// ------------------------------------------------------------------- 实体
struct Semester {
    int id = 0;
    std::string code;      // 2026-2027-1
    std::string name;      // 2026-2027 学年第一学期
    std::string start_date;
    int total_weeks = 16;
    bool is_current = false;
};

struct Course {
    int id = 0;
    std::string code;      // CS201
    std::string name;      // 数据结构与算法
    std::string dept;
    double credits = 0.0;
    int total_hours = 0;
    CourseType type = CourseType::Required;
    int hours_per_week = 2;   // 每周需要排的大节数
    bool active = true;
};

// DAG 的一条边: prereq -> course (先修课 -> 后续课)
struct PrereqEdge {
    int course_id = 0;
    int prereq_course_id = 0;
};

struct Teacher {
    int id = 0;
    std::string no;
    std::string name;
    std::string title;
    std::string dept;
    int max_classes = 4;
};

struct Classroom {
    int id = 0;
    std::string code;
    std::string building;
    int capacity = 0;
    RoomType type = RoomType::General;
};

struct TimeSlot {
    int id = 0;
    int day_of_week = 1;   // 1-7
    int period_no = 1;     // 1-5
    std::string start_time;
    std::string end_time;
    int weight = 50;       // 越小越优先占用
};

struct TeachingClass {
    int id = 0;
    std::string code;      // CS201-01
    int course_id = 0;
    int teacher_id = 0;
    std::string semester;
    std::string name;
    int capacity = 0;
    int enrolled = 0;
    int expected_students = 0;
    bool open_for_enroll = true;
};

struct ScheduleEntry {
    int id = 0;
    int teaching_class_id = 0;
    int course_id = 0;
    int teacher_id = 0;
    int classroom_id = 0;
    std::string semester;
    int day_of_week = 1;
    int period_no = 1;
};

struct Student {
    int id = 0;
    std::string no;
    std::string name;
    std::string gender;      // M / F
    int grade_year = 0;
    std::string major;
    std::string admin_class;
    std::string dept;
    std::string status;      // active / suspended / graduated
};

struct Enrollment {
    int id = 0;
    int student_id = 0;
    int teaching_class_id = 0;
    std::string semester;
    EnrollmentStatus status = EnrollmentStatus::Enrolled;
    std::optional<double> score;
};

// 排课算法在内存中产生的"时段+教室"分配方案
struct Placement {
    int teaching_class_id = 0;
    int course_id = 0;
    int teacher_id = 0;
    int classroom_id = 0;
    int day_of_week = 1;
    int period_no = 1;
};

// 学生课表的一行(供终端周课表网格渲染)
struct TimetableItem {
    std::string class_code;
    std::string course_code;
    std::string course_name;
    std::string teacher_name;
    std::string room_code;
    std::string building;
    double credits = 0.0;
    int day_of_week = 0;     // 0 表示该教学班尚未排课
    int period_no = 0;
    std::optional<double> score;
    EnrollmentStatus status = EnrollmentStatus::Enrolled;
};

}  // namespace scheduler
