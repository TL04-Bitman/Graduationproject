// ============================================================================
//  services/enrollment_service.hpp —— 学生端: 登录 / 选课 / 退课 / 查课 / 先修进度
//
//  选课校验链(任何一步不通过都会给出明确的中文原因, 并在异常中携带明细):
//      ① 学籍状态是否正常
//      ② 教学班是否存在、是否开放选课、是否已完成排课(可按配置关闭)
//      ③ 是否已经选过(幂等保护)
//      ④ 先修课程是否全部通过   -> rules::missing_prerequisites
//      ⑤ 是否超出学期学分上限   -> rules::exceeds_credit_limit
//      ⑥ 教学班容量是否已满
//      ⑦ 与已选课程是否时间冲突 -> rules::find_time_conflicts
//  通过后在同一事务内写入选课记录并刷新班级人数, 保证数据一致。
// ============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "scheduler/db/database.hpp"
#include "scheduler/repositories.hpp"
#include "scheduler/types.hpp"

namespace scheduler::service {

class EnrollmentService {
public:
    explicit EnrollmentService(db::Database& database)
        : db_(database), catalog_(database), teaching_(database), students_(database) {}

    // ------------------------------------------------------------------ 登录
    // 口令校验方式: SHA2(CONCAT(学号, '#', 明文口令), 256) 与库中摘要比对,
    // 即"学号作为唯一盐值"(演示实现; 生产环境应改用 bcrypt/argon2)。
    std::optional<Student> login(const std::string& student_no, const std::string& password);

    // ------------------------------------------------------------ 可选课程列表
    struct ClassOption {
        TeachingClass teaching_class;
        Course course;
        Teacher teacher;
        std::string room_text;      // 已排课时的教室
        std::string slot_text;      // 已排课时的时段
        std::string status;         // 可选 / 已选 / 先修未满足 / 时间冲突 / 容量已满 / 尚未排课 / 未开放
        bool selectable = false;
        std::vector<std::string> missing_prerequisites;
        std::vector<std::string> conflicts;
    };
    std::vector<ClassOption> class_options(const std::string& student_no,
                                           std::string semester = "");

    // ------------------------------------------------------------------ 选课
    struct EnrollResult {
        std::string message;
        TeachingClass teaching_class;
        double credits_after = 0.0;
    };
    // 失败时抛出 PrerequisiteError / TimeConflictError / CreditLimitError /
    // CapacityError / EnrollmentClosedError / NotScheduledError / NotFoundError
    EnrollResult enroll(const std::string& student_no, const std::string& class_code,
                        std::string semester = "");

    // ------------------------------------------------------------------ 退课
    struct DropResult {
        std::string message;
        double credits_after = 0.0;
    };
    DropResult drop(const std::string& student_no, const std::string& class_code,
                    std::string semester = "");

    // ------------------------------------------------------------ 我的课表
    struct TimetableResult {
        Student student;
        std::string semester;
        std::vector<TimetableItem> items;
        std::vector<std::string> unscheduled;   // 已选但尚未排课的课程(等教务排课)
        double total_credits = 0.0;
        int scheduled_count = 0;
    };
    TimetableResult timetable(const std::string& student_no, std::string semester = "");

    // ------------------------------------------------------- 先修进度 / 学分
    struct PrereqProgress {
        std::string course_code;
        std::string course_name;
        double credits = 0.0;
        std::vector<std::string> prerequisites;
        std::vector<std::string> satisfied;
        std::vector<std::string> missing;
    };
    std::vector<PrereqProgress> prereq_progress(const std::string& student_no);

    double credits_of(const std::string& student_no, const std::string& semester);

private:
    // 解析学期参数(空则取配置中的当前学期)
    std::string resolve_semester(const std::string& semester) const;
    Student require_student(const std::string& student_no);
    TeachingClass require_class(const std::string& class_code);

    db::Database& db_;
    repo::CatalogRepository catalog_;
    repo::TeachingRepository teaching_;
    repo::StudentRepository students_;
};

}  // namespace scheduler::service
