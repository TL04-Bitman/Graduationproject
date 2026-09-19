// ============================================================================
//  repositories/catalog_repo.cpp —— 学期/课程/先修关系/教学资源 的读写实现
// ============================================================================
#include <algorithm>

#include "scheduler/repositories.hpp"
#include "scheduler/util/str.hpp"

namespace scheduler::repo {
namespace {

Semester semester_from_row(const db::Row& row) {
    Semester semester;
    semester.id = row.get_int("id");
    semester.code = row.get("semester_code");
    semester.name = row.get("semester_name");
    semester.start_date = row.get("start_date");
    semester.total_weeks = row.get_int("total_weeks", 16);
    semester.is_current = row.get_bool("is_current");
    return semester;
}

Course course_from_row(const db::Row& row) {
    Course course;
    course.id = row.get_int("id");
    course.code = row.get("course_code");
    course.name = row.get("course_name");
    course.dept = row.get("dept");
    course.credits = row.get_double("credits");
    course.total_hours = row.get_int("total_hours");
    course.type = course_type_from_db(row.get("course_type"));
    course.hours_per_week = row.get_int("class_hours_per_week", 2);
    course.active = row.get("status") == "active";
    return course;
}

const char* kCourseColumns =
        "id, course_code, course_name, dept, credits, total_hours, course_type, "
        "class_hours_per_week, status";
const char* kSemesterColumns =
        "id, semester_code, semester_name, "
        "DATE_FORMAT(start_date, '%Y-%m-%d') AS start_date, total_weeks, is_current";

}  // namespace

// ------------------------------------------------------------------- 学期
std::vector<Semester> CatalogRepository::semesters() const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kSemesterColumns + " FROM semesters ORDER BY semester_code");
    std::vector<Semester> list;
    for (const db::Row& row : result.rows()) list.push_back(semester_from_row(row));
    return list;
}

std::optional<Semester> CatalogRepository::semester_by_code(const std::string& code) const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kSemesterColumns + " FROM semesters WHERE semester_code = ?",
            {code});
    if (result.empty()) return std::nullopt;
    return semester_from_row(result.first());
}

std::optional<Semester> CatalogRepository::current_semester() const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kSemesterColumns +
            " FROM semesters WHERE is_current = 1 ORDER BY semester_code DESC LIMIT 1");
    if (result.empty()) return std::nullopt;
    return semester_from_row(result.first());
}

// ------------------------------------------------------------------- 课程
std::vector<Course> CatalogRepository::courses(bool only_active) const {
    const std::string sql = std::string("SELECT ") + kCourseColumns + " FROM courses" +
                            (only_active ? " WHERE status = 'active'" : "") +
                            " ORDER BY course_code";
    const db::ResultSet result = db_.query(sql);
    std::vector<Course> list;
    for (const db::Row& row : result.rows()) list.push_back(course_from_row(row));
    return list;
}

std::map<int, Course> CatalogRepository::course_map(bool only_active) const {
    std::map<int, Course> mapping;
    for (Course& course : courses(only_active)) {
        mapping[course.id] = std::move(course);
    }
    return mapping;
}

std::optional<Course> CatalogRepository::course_by_code(const std::string& code) const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kCourseColumns + " FROM courses WHERE course_code = ?", {code});
    if (result.empty()) return std::nullopt;
    return course_from_row(result.first());
}

std::optional<Course> CatalogRepository::course_by_id(int id) const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kCourseColumns + " FROM courses WHERE id = ?",
            {std::to_string(id)});
    if (result.empty()) return std::nullopt;
    return course_from_row(result.first());
}

// --------------------------------------------------------------- 先修关系
std::vector<PrereqEdge> CatalogRepository::prereq_edges() const {
    const db::ResultSet result = db_.query(
            "SELECT course_id, prereq_course_id FROM course_prerequisites "
            "ORDER BY prereq_course_id, course_id");
    std::vector<PrereqEdge> edges;
    edges.reserve(result.size());
    for (const db::Row& row : result.rows()) {
        edges.push_back({row.get_int("course_id"), row.get_int("prereq_course_id")});
    }
    return edges;
}

std::vector<std::string> CatalogRepository::prereq_codes_of(const std::string& course_code) const {
    const db::ResultSet result = db_.query(
            "SELECT pc.course_code AS prereq_code "
            "FROM course_prerequisites p "
            "JOIN courses c  ON c.id  = p.course_id "
            "JOIN courses pc ON pc.id = p.prereq_course_id "
            "WHERE c.course_code = ? ORDER BY pc.course_code",
            {course_code});
    std::vector<std::string> codes;
    codes.reserve(result.size());
    for (const db::Row& row : result.rows()) codes.push_back(row.get("prereq_code"));
    return codes;
}

bool CatalogRepository::has_prerequisite(const std::string& course_code,
                                         const std::string& prereq_code) const {
    const db::ResultSet result = db_.query(
            "SELECT 1 AS hit FROM course_prerequisites p "
            "JOIN courses c  ON c.id  = p.course_id "
            "JOIN courses pc ON pc.id = p.prereq_course_id "
            "WHERE c.course_code = ? AND pc.course_code = ? LIMIT 1",
            {course_code, prereq_code});
    return !result.empty();
}

bool CatalogRepository::add_prerequisite(const std::string& course_code,
                                         const std::string& prereq_code) {
    const std::uint64_t affected = db_.execute(
            "INSERT INTO course_prerequisites (course_id, prereq_course_id) "
            "SELECT c.id, p.id FROM courses c JOIN courses p "
            "WHERE c.course_code = ? AND p.course_code = ?",
            {course_code, prereq_code});
    return affected > 0;
}

bool CatalogRepository::remove_prerequisite(const std::string& course_code,
                                            const std::string& prereq_code) {
    const std::uint64_t affected = db_.execute(
            "DELETE p FROM course_prerequisites p "
            "JOIN courses c  ON c.id  = p.course_id "
            "JOIN courses pc ON pc.id = p.prereq_course_id "
            "WHERE c.course_code = ? AND pc.course_code = ?",
            {course_code, prereq_code});
    return affected > 0;
}

int CatalogRepository::clear_prerequisites() {
    return static_cast<int>(db_.execute("DELETE FROM course_prerequisites"));
}

// --------------------------------------------------------------- 教学资源
std::vector<Teacher> CatalogRepository::teachers() const {
    const db::ResultSet result = db_.query(
            "SELECT id, teacher_no, teacher_name, title, dept, max_classes "
            "FROM teachers ORDER BY teacher_no");
    std::vector<Teacher> list;
    for (const db::Row& row : result.rows()) {
        Teacher teacher;
        teacher.id = row.get_int("id");
        teacher.no = row.get("teacher_no");
        teacher.name = row.get("teacher_name");
        teacher.title = row.get("title");
        teacher.dept = row.get("dept");
        teacher.max_classes = row.get_int("max_classes", 4);
        list.push_back(std::move(teacher));
    }
    return list;
}

std::map<int, Teacher> CatalogRepository::teacher_map() const {
    std::map<int, Teacher> mapping;
    for (Teacher& teacher : teachers()) {
        mapping[teacher.id] = std::move(teacher);
    }
    return mapping;
}

std::vector<Classroom> CatalogRepository::classrooms() const {
    const db::ResultSet result = db_.query(
            "SELECT id, room_code, building, capacity, room_type FROM classrooms "
            "ORDER BY room_code");
    std::vector<Classroom> list;
    for (const db::Row& row : result.rows()) {
        Classroom room;
        room.id = row.get_int("id");
        room.code = row.get("room_code");
        room.building = row.get("building");
        room.capacity = row.get_int("capacity");
        room.type = room_type_from_db(row.get("room_type"));
        list.push_back(std::move(room));
    }
    return list;
}

std::map<int, Classroom> CatalogRepository::classroom_map() const {
    std::map<int, Classroom> mapping;
    for (Classroom& room : classrooms()) {
        mapping[room.id] = std::move(room);
    }
    return mapping;
}

std::vector<TimeSlot> CatalogRepository::time_slots() const {
    const db::ResultSet result = db_.query(
            "SELECT id, day_of_week, period_no, "
            "TIME_FORMAT(start_time, '%H:%i:%s') AS start_time, "
            "TIME_FORMAT(end_time, '%H:%i:%s') AS end_time, slot_weight "
            "FROM time_slots ORDER BY day_of_week, period_no");
    std::vector<TimeSlot> list;
    for (const db::Row& row : result.rows()) {
        TimeSlot slot;
        slot.id = row.get_int("id");
        slot.day_of_week = row.get_int("day_of_week", 1);
        slot.period_no = row.get_int("period_no", 1);
        slot.start_time = row.get("start_time");
        slot.end_time = row.get("end_time");
        slot.weight = row.get_int("slot_weight", 50);
        list.push_back(std::move(slot));
    }
    return list;
}

}  // namespace scheduler::repo
