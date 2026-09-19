// ============================================================================
//  repositories.hpp —— 数据访问层(DAO)
//
//  分层约定:
//      db::Database 只懂"连接与 SQL"; 本层的每个 Repository 只懂"某张表的读写",
//      把结果集翻译成领域实体(types.hpp); 业务规则与流程编排一律放在 services/。
//      这样任何一条 SQL 都能被快速定位、审查与优化(见 docs/DATABASE.md)。
//
//  安全约定:
//      所有带外部输入的 SQL 一律使用占位符 ? + 参数绑定(prepared statement),
//      禁止字符串拼接, 从源头杜绝 SQL 注入。
// ============================================================================
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "scheduler/db/database.hpp"
#include "scheduler/types.hpp"

namespace scheduler::repo {

// ---------------------------------------------------------------------------
//  基础数据: 学期 / 课程 / 先修关系 / 教师 / 教室 / 节次
// ---------------------------------------------------------------------------
class CatalogRepository {
public:
    explicit CatalogRepository(db::Database& database) : db_(database) {}

    // ---- 学期 ----
    std::vector<Semester> semesters() const;
    std::optional<Semester> semester_by_code(const std::string& code) const;
    std::optional<Semester> current_semester() const;

    // ---- 课程 ----
    std::vector<Course> courses(bool only_active = true) const;
    std::map<int, Course> course_map(bool only_active = true) const;
    std::optional<Course> course_by_code(const std::string& code) const;
    std::optional<Course> course_by_id(int id) const;

    // ---- 先修关系(DAG 的边) ----
    std::vector<PrereqEdge> prereq_edges() const;
    std::vector<std::string> prereq_codes_of(const std::string& course_code) const;
    bool has_prerequisite(const std::string& course_code, const std::string& prereq_code) const;
    // 插入/删除先修边; 返回是否真的改动(重复插入或不存在时返回 false)
    bool add_prerequisite(const std::string& course_code, const std::string& prereq_code);
    bool remove_prerequisite(const std::string& course_code, const std::string& prereq_code);
    int clear_prerequisites();   // 演示数据重置用

    // ---- 资源 ----
    std::vector<Teacher> teachers() const;
    std::map<int, Teacher> teacher_map() const;
    std::vector<Classroom> classrooms() const;
    std::map<int, Classroom> classroom_map() const;
    std::vector<TimeSlot> time_slots() const;

private:
    db::Database& db_;
};

// ---------------------------------------------------------------------------
//  教学班 / 排课结果 / 排课批次审计
// ---------------------------------------------------------------------------
struct SchedulingRunRecord {
    long long id = 0;
    std::string semester;
    std::string algorithm;
    std::string started_at;
    std::string finished_at;
    long long duration_ms = 0;
    int total_courses = 0;
    int total_classes = 0;
    int scheduled_entries = 0;
    int failed_classes = 0;
    std::string status;
    std::string message;
};

class TeachingRepository {
public:
    explicit TeachingRepository(db::Database& database) : db_(database) {}

    std::vector<TeachingClass> classes_of_semester(const std::string& semester) const;
    std::map<int, TeachingClass> class_map(const std::string& semester) const;
    std::optional<TeachingClass> class_by_code(const std::string& class_code) const;
    std::optional<TeachingClass> class_by_id(int id) const;

    // ---- 排课结果 ----
    std::vector<ScheduleEntry> schedule_of_semester(const std::string& semester) const;
    std::vector<ScheduleEntry> schedule_of_class(int class_id) const;
    std::set<int> scheduled_class_ids(const std::string& semester) const;
    int delete_schedule(const std::string& semester);
    // 批量写入排课结果(在调用方的事务中执行, 保证与审计记录同生共死)
    int insert_schedule_entries(db::Database& transaction_db,
                                const std::vector<ScheduleEntry>& entries);

    int sync_enrolled_counts();   // 校正 teaching_classes.enrolled_count

    // ---- 审计 ----
    long long insert_run(const SchedulingRunRecord& record);
    std::vector<SchedulingRunRecord> recent_runs(int limit) const;

private:
    db::Database& db_;
};

// ---------------------------------------------------------------------------
//  学生 / 选课记录
// ---------------------------------------------------------------------------
class StudentRepository {
public:
    explicit StudentRepository(db::Database& database) : db_(database) {}

    std::vector<Student> students() const;
    std::optional<Student> student_by_no(const std::string& student_no) const;
    std::optional<Student> student_by_id(int student_id) const;
    std::string password_hash(const std::string& student_no) const;   // 登录校验用(含唯一盐)

    // ---- 历史成绩相关 ----
    // 已通过的课程编号集合(status=completed 且 score >= min_score)
    std::set<std::string> passed_course_codes(int student_id, int min_score) const;
    std::map<std::string, double> completed_scores(int student_id) const;
    std::map<std::string, std::string> course_status_map(int student_id) const;  // 课程编号 -> 状态标签

    // ---- 选课 ----
    std::vector<Enrollment> enrollments_of_semester(int student_id, const std::string& semester) const;
    std::vector<Enrollment> enrollment_history(int student_id) const;
    int insert_enrollment(int student_id, int teaching_class_id, const std::string& semester);
    bool drop_enrollment(int student_id, const std::string& class_code,
                         const std::string& semester);
    bool is_enrolled(int student_id, int teaching_class_id, const std::string& semester) const;
    double credits_of_semester(int student_id, const std::string& semester) const;
    int enrolled_count(int teaching_class_id) const;
    void refresh_class_enrolled(int teaching_class_id);

    // ---- 查课 ----
    std::vector<TimetableItem> timetable(int student_id, const std::string& semester) const;

private:
    db::Database& db_;
};

}  // namespace scheduler::repo
