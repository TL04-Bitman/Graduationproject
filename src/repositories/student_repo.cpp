// ============================================================================
//  repositories/student_repo.cpp —— 学生 / 选课记录 / 个人课表
// ============================================================================
#include <algorithm>

#include "scheduler/repositories.hpp"
#include "scheduler/util/str.hpp"

namespace scheduler::repo {
namespace {

Student student_from_row(const db::Row& row) {
    Student student;
    student.id = row.get_int("id");
    student.no = row.get("student_no");
    student.name = row.get("student_name");
    student.gender = row.get("gender");
    student.grade_year = row.get_int("grade_year");
    student.major = row.get("major");
    student.admin_class = row.get("admin_class");
    student.dept = row.get("dept");
    student.status = row.get("status");
    return student;
}

Enrollment enrollment_from_row(const db::Row& row) {
    Enrollment enrollment;
    enrollment.id = row.get_int("id");
    enrollment.student_id = row.get_int("student_id");
    enrollment.teaching_class_id = row.get_int("teaching_class_id");
    enrollment.semester = row.get("semester_code");
    enrollment.status = enrollment_status_from_db(row.get("status"));
    enrollment.score = row.get_nullable_double("score");
    return enrollment;
}

const char* kStudentColumns =
        "id, student_no, student_name, gender, grade_year, major, admin_class, dept, status";

}  // namespace

// ------------------------------------------------------------------- 学生
std::vector<Student> StudentRepository::students() const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kStudentColumns + " FROM students ORDER BY student_no");
    std::vector<Student> list;
    for (const db::Row& row : result.rows()) list.push_back(student_from_row(row));
    return list;
}

std::optional<Student> StudentRepository::student_by_no(const std::string& student_no) const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kStudentColumns + " FROM students WHERE student_no = ?",
            {student_no});
    if (result.empty()) return std::nullopt;
    return student_from_row(result.first());
}

std::optional<Student> StudentRepository::student_by_id(int student_id) const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kStudentColumns + " FROM students WHERE id = ?",
            {std::to_string(student_id)});
    if (result.empty()) return std::nullopt;
    return student_from_row(result.first());
}

std::string StudentRepository::password_hash(const std::string& student_no) const {
    const db::ResultSet result = db_.query(
            "SELECT password_hash FROM students WHERE student_no = ?", {student_no});
    return result.empty() ? std::string() : result.first().get("password_hash");
}

// ------------------------------------------------------- 先修/成绩(历史)
std::set<std::string> StudentRepository::passed_course_codes(int student_id, int min_score) const {
    // 判据: 状态为 completed(任课教师已录入成绩) 且 成绩 >= 及格线
    const db::ResultSet result = db_.query(
            "SELECT DISTINCT c.course_code AS course_code "
            "FROM enrollments e "
            "JOIN teaching_classes tc ON tc.id = e.teaching_class_id "
            "JOIN courses c ON c.id = tc.course_id "
            "WHERE e.student_id = ? AND e.status = 'completed' "
            "  AND e.score IS NOT NULL AND e.score >= ?",
            {std::to_string(student_id), std::to_string(min_score)});
    std::set<std::string> codes;
    for (const db::Row& row : result.rows()) codes.insert(row.get("course_code"));
    return codes;
}

std::map<std::string, double> StudentRepository::completed_scores(int student_id) const {
    const db::ResultSet result = db_.query(
            "SELECT c.course_code AS course_code, MAX(e.score) AS best_score "
            "FROM enrollments e "
            "JOIN teaching_classes tc ON tc.id = e.teaching_class_id "
            "JOIN courses c ON c.id = tc.course_id "
            "WHERE e.student_id = ? AND e.status = 'completed' "
            "GROUP BY c.course_code",
            {std::to_string(student_id)});
    std::map<std::string, double> scores;
    for (const db::Row& row : result.rows()) {
        scores[row.get("course_code")] = row.get_double("best_score");
    }
    return scores;
}

std::map<std::string, std::string> StudentRepository::course_status_map(int student_id) const {
    const db::ResultSet result = db_.query(
            "SELECT c.course_code AS course_code, e.status AS status, e.score AS score, "
            "e.semester_code AS semester_code "
            "FROM enrollments e "
            "JOIN teaching_classes tc ON tc.id = e.teaching_class_id "
            "JOIN courses c ON c.id = tc.course_id "
            "WHERE e.student_id = ? AND e.status <> 'dropped' "
            "ORDER BY e.semester_code DESC",
            {std::to_string(student_id)});
    std::map<std::string, std::string> status_map;
    for (const db::Row& row : result.rows()) {
        const std::string code = row.get("course_code");
        if (status_map.find(code) != status_map.end()) continue;   // 保留最近一次修读记录
        const std::string status = row.get("status");
        std::string text = status == "completed"   ? "已完成"
                           : status == "failed" ? "不及格"
                                                : "在修";
        if (!row.is_null("score")) {
            text += "(" + str::number(row.get_double("score"), 0) + "分)";
        }
        status_map[code] = text;
    }
    return status_map;
}

// --------------------------------------------------------------- 选课记录
std::vector<Enrollment> StudentRepository::enrollments_of_semester(
        int student_id, const std::string& semester) const {
    const db::ResultSet result = db_.query(
            "SELECT id, student_id, teaching_class_id, semester_code, status, score "
            "FROM enrollments WHERE student_id = ? AND semester_code = ? "
            "ORDER BY id",
            {std::to_string(student_id), semester});
    std::vector<Enrollment> list;
    for (const db::Row& row : result.rows()) list.push_back(enrollment_from_row(row));
    return list;
}

std::vector<Enrollment> StudentRepository::enrollment_history(int student_id) const {
    const db::ResultSet result = db_.query(
            "SELECT id, student_id, teaching_class_id, semester_code, status, score "
            "FROM enrollments WHERE student_id = ? ORDER BY semester_code DESC, id",
            {std::to_string(student_id)});
    std::vector<Enrollment> list;
    for (const db::Row& row : result.rows()) list.push_back(enrollment_from_row(row));
    return list;
}

int StudentRepository::insert_enrollment(int student_id, int teaching_class_id,
                                         const std::string& semester) {
    return static_cast<int>(db_.execute(
            "INSERT INTO enrollments (student_id, teaching_class_id, semester_code, status) "
            "VALUES (?, ?, ?, 'enrolled')",
            {std::to_string(student_id), std::to_string(teaching_class_id), semester}));
}

// 退课采用"状态置为 dropped"的软删除: 保留选课历史, 便于审计与成绩追溯
bool StudentRepository::drop_enrollment(int student_id, const std::string& class_code,
                                        const std::string& semester) {
    const std::uint64_t affected = db_.execute(
            "UPDATE enrollments e "
            "JOIN teaching_classes tc ON tc.id = e.teaching_class_id "
            "SET e.status = 'dropped' "
            "WHERE e.student_id = ? AND tc.class_code = ? AND e.semester_code = ? "
            "  AND e.status = 'enrolled'",
            {std::to_string(student_id), class_code, semester});
    return affected > 0;
}

bool StudentRepository::is_enrolled(int student_id, int teaching_class_id,
                                    const std::string& semester) const {
    const db::ResultSet result = db_.query(
            "SELECT 1 AS hit FROM enrollments "
            "WHERE student_id = ? AND teaching_class_id = ? AND semester_code = ? "
            "  AND status IN ('enrolled','completed') LIMIT 1",
            {std::to_string(student_id), std::to_string(teaching_class_id), semester});
    return !result.empty();
}

double StudentRepository::credits_of_semester(int student_id, const std::string& semester) const {
    const db::ResultSet result = db_.query(
            "SELECT COALESCE(SUM(c.credits), 0) AS total_credits "
            "FROM enrollments e "
            "JOIN teaching_classes tc ON tc.id = e.teaching_class_id "
            "JOIN courses c ON c.id = tc.course_id "
            "WHERE e.student_id = ? AND e.semester_code = ? "
            "  AND e.status IN ('enrolled','completed')",
            {std::to_string(student_id), semester});
    return result.empty() ? 0.0 : result.first().get_double("total_credits");
}

int StudentRepository::enrolled_count(int teaching_class_id) const {
    const db::ResultSet result = db_.query(
            "SELECT COUNT(*) AS total FROM enrollments "
            "WHERE teaching_class_id = ? AND status IN ('enrolled','completed')",
            {std::to_string(teaching_class_id)});
    return result.empty() ? 0 : result.first().get_int("total");
}

void StudentRepository::refresh_class_enrolled(int teaching_class_id) {
    db_.execute(
            "UPDATE teaching_classes tc SET tc.enrolled_count = ("
            "  SELECT COUNT(*) FROM enrollments e WHERE e.teaching_class_id = tc.id "
            "  AND e.status IN ('enrolled','completed')) WHERE tc.id = ?",
            {std::to_string(teaching_class_id)});
}

// ------------------------------------------------------------------- 课表
// 直接复用数据库视图 v_student_timetable: 把"学生-教学班-课程-教师-教室-时段"
// 的多表 JOIN 固化在库侧, 应用层只做展示, 减少 C++ 侧的样板代码。
std::vector<TimetableItem> StudentRepository::timetable(int student_id,
                                                        const std::string& semester) const {
    const db::ResultSet result = db_.query(
            "SELECT course_code, course_name, class_code, teacher_name, room_code, building, "
            "credits, day_of_week, period_no, enroll_status, score "
            "FROM v_student_timetable WHERE student_id = ? AND semester_code = ? "
            "ORDER BY day_of_week IS NULL, day_of_week, period_no",
            {std::to_string(student_id), semester});
    std::vector<TimetableItem> items;
    items.reserve(result.size());
    for (const db::Row& row : result.rows()) {
        TimetableItem item;
        item.course_code = row.get("course_code");
        item.course_name = row.get("course_name");
        item.class_code = row.get("class_code");
        item.teacher_name = row.get("teacher_name");
        item.room_code = row.get("room_code");
        item.building = row.get("building");
        item.credits = row.get_double("credits");
        item.day_of_week = row.get_int("day_of_week", 0);
        item.period_no = row.get_int("period_no", 0);
        item.status = enrollment_status_from_db(row.get("enroll_status"));
        item.score = row.get_nullable_double("score");
        items.push_back(std::move(item));
    }
    return items;
}

}  // namespace scheduler::repo
