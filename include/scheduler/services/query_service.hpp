// ============================================================================
//  services/query_service.hpp —— 教务端查询与统计
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "scheduler/db/database.hpp"
#include "scheduler/repositories.hpp"
#include "scheduler/types.hpp"

namespace scheduler::service {

class QueryService {
public:
    explicit QueryService(db::Database& database)
        : db_(database), catalog_(database), teaching_(database), students_(database) {}

    // 全校(某学期)课表: 每条记录包含班级/课程/教师/教室/时段
    struct ClassRow {
        std::string class_code;
        std::string course_code;
        std::string course_name;
        std::string teacher_name;
        std::string room_text;
        std::string slot_text;
        int enrolled = 0;
        int capacity = 0;
        bool scheduled = false;
    };
    std::vector<ClassRow> class_rows(const std::string& semester);

    // 教师工作量(教学班数 / 每周课次 / 时段分布)
    struct TeacherLoad {
        std::string teacher_no;
        std::string teacher_name;
        std::string title;
        int class_count = 0;
        int session_count = 0;
        int max_classes = 4;
        bool overloaded = false;
    };
    std::vector<TeacherLoad> teacher_loads(const std::string& semester);

    // 教室使用情况(已占用时段数 / 总时段数 / 使用率)
    struct RoomUsage {
        std::string room_code;
        std::string building;
        std::string type_text;
        int capacity = 0;
        int used_slots = 0;
        int total_slots = 0;
        double usage = 0.0;
    };
    std::vector<RoomUsage> room_usage(const std::string& semester);

    // 学生名单及本学期选课情况
    struct StudentRow {
        std::string student_no;
        std::string student_name;
        std::string major;
        std::string admin_class;
        int grade_year = 0;
        int selected_classes = 0;
        double credits = 0.0;
    };
    std::vector<StudentRow> student_rows(const std::string& semester);

    // 排课批次历史(审计)
    std::vector<repo::SchedulingRunRecord> run_history(int limit);

private:
    db::Database& db_;
    repo::CatalogRepository catalog_;
    repo::TeachingRepository teaching_;
    repo::StudentRepository students_;
};

}  // namespace scheduler::service
