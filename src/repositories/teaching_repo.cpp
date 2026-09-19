// ============================================================================
//  repositories/teaching_repo.cpp —— 教学班 / 排课结果 / 排课审计
// ============================================================================
#include <algorithm>
#include <string>

#include "scheduler/repositories.hpp"
#include "scheduler/util/str.hpp"

namespace scheduler::repo {
namespace {

TeachingClass class_from_row(const db::Row& row) {
    TeachingClass teaching_class;
    teaching_class.id = row.get_int("id");
    teaching_class.code = row.get("class_code");
    teaching_class.course_id = row.get_int("course_id");
    teaching_class.teacher_id = row.get_int("teacher_id");
    teaching_class.semester = row.get("semester_code");
    teaching_class.name = row.get("class_name");
    teaching_class.capacity = row.get_int("capacity");
    teaching_class.enrolled = row.get_int("enrolled_count");
    teaching_class.expected_students = row.get_int("expected_students");
    teaching_class.open_for_enroll = row.get_bool("open_for_enroll");
    return teaching_class;
}

ScheduleEntry entry_from_row(const db::Row& row) {
    ScheduleEntry entry;
    entry.id = row.get_int("id");
    entry.teaching_class_id = row.get_int("teaching_class_id");
    entry.course_id = row.get_int("course_id");
    entry.teacher_id = row.get_int("teacher_id");
    entry.classroom_id = row.get_int("classroom_id");
    entry.semester = row.get("semester_code");
    entry.day_of_week = row.get_int("day_of_week", 1);
    entry.period_no = row.get_int("period_no", 1);
    return entry;
}

SchedulingRunRecord run_from_row(const db::Row& row) {
    SchedulingRunRecord record;
    record.id = row.get_long("id");
    record.semester = row.get("semester_code");
    record.algorithm = row.get("algorithm");
    record.started_at = row.get("started_at");
    record.finished_at = row.get("finished_at");
    record.duration_ms = row.get_long("duration_ms");
    record.total_courses = row.get_int("total_courses");
    record.total_classes = row.get_int("total_classes");
    record.scheduled_entries = row.get_int("scheduled_entries");
    record.failed_classes = row.get_int("failed_classes");
    record.status = row.get("status");
    record.message = row.get("message");
    return record;
}

const char* kClassColumns =
        "id, class_code, course_id, teacher_id, semester_code, class_name, capacity, "
        "enrolled_count, expected_students, open_for_enroll";
const char* kEntryColumns =
        "id, teaching_class_id, course_id, teacher_id, classroom_id, semester_code, "
        "day_of_week, period_no";

}  // namespace

// --------------------------------------------------------------- 教学班
std::vector<TeachingClass> TeachingRepository::classes_of_semester(const std::string& semester) const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kClassColumns +
            " FROM teaching_classes WHERE semester_code = ? ORDER BY class_code",
            {semester});
    std::vector<TeachingClass> list;
    for (const db::Row& row : result.rows()) list.push_back(class_from_row(row));
    return list;
}

std::map<int, TeachingClass> TeachingRepository::class_map(const std::string& semester) const {
    std::map<int, TeachingClass> mapping;
    for (TeachingClass& item : classes_of_semester(semester)) {
        mapping[item.id] = std::move(item);
    }
    return mapping;
}

std::optional<TeachingClass> TeachingRepository::class_by_code(const std::string& class_code) const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kClassColumns + " FROM teaching_classes WHERE class_code = ?",
            {class_code});
    if (result.empty()) return std::nullopt;
    return class_from_row(result.first());
}

std::optional<TeachingClass> TeachingRepository::class_by_id(int id) const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kClassColumns + " FROM teaching_classes WHERE id = ?",
            {std::to_string(id)});
    if (result.empty()) return std::nullopt;
    return class_from_row(result.first());
}

// --------------------------------------------------------------- 排课结果
std::vector<ScheduleEntry> TeachingRepository::schedule_of_semester(const std::string& semester) const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kEntryColumns +
            " FROM schedule_entries WHERE semester_code = ? "
            "ORDER BY day_of_week, period_no, teaching_class_id",
            {semester});
    std::vector<ScheduleEntry> list;
    for (const db::Row& row : result.rows()) list.push_back(entry_from_row(row));
    return list;
}

std::vector<ScheduleEntry> TeachingRepository::schedule_of_class(int class_id) const {
    const db::ResultSet result = db_.query(
            std::string("SELECT ") + kEntryColumns +
            " FROM schedule_entries WHERE teaching_class_id = ? ORDER BY day_of_week, period_no",
            {std::to_string(class_id)});
    std::vector<ScheduleEntry> list;
    for (const db::Row& row : result.rows()) list.push_back(entry_from_row(row));
    return list;
}

std::set<int> TeachingRepository::scheduled_class_ids(const std::string& semester) const {
    const db::ResultSet result = db_.query(
            "SELECT DISTINCT teaching_class_id FROM schedule_entries WHERE semester_code = ?",
            {semester});
    std::set<int> ids;
    for (const db::Row& row : result.rows()) ids.insert(row.get_int("teaching_class_id"));
    return ids;
}

int TeachingRepository::delete_schedule(const std::string& semester) {
    return static_cast<int>(
            db_.execute("DELETE FROM schedule_entries WHERE semester_code = ?", {semester}));
}

// 批量写入排课结果: 多值 INSERT + 参数绑定。
// 每批 200 行(单条语句 1400 个占位符以内), 兼顾语句长度与网络往返次数。
int TeachingRepository::insert_schedule_entries(db::Database& transaction_db,
                                               const std::vector<ScheduleEntry>& entries) {
    if (entries.empty()) return 0;
    constexpr std::size_t kBatchSize = 200;
    int inserted = 0;
    for (std::size_t offset = 0; offset < entries.size(); offset += kBatchSize) {
        const std::size_t end = std::min(offset + kBatchSize, entries.size());
        std::string sql =
                "INSERT INTO schedule_entries (teaching_class_id, course_id, teacher_id, "
                "classroom_id, semester_code, day_of_week, period_no, week_start, week_end) "
                "VALUES ";
        std::vector<std::string> parameters;
        parameters.reserve((end - offset) * 7);
        for (std::size_t i = offset; i < end; ++i) {
            const ScheduleEntry& entry = entries[i];
            if (i != offset) sql += ", ";
            sql += "(?, ?, ?, ?, ?, ?, ?, 1, 16)";
            parameters.push_back(std::to_string(entry.teaching_class_id));
            parameters.push_back(std::to_string(entry.course_id));
            parameters.push_back(std::to_string(entry.teacher_id));
            parameters.push_back(std::to_string(entry.classroom_id));
            parameters.push_back(entry.semester);
            parameters.push_back(std::to_string(entry.day_of_week));
            parameters.push_back(std::to_string(entry.period_no));
        }
        inserted += static_cast<int>(transaction_db.execute(sql, parameters));
    }
    return inserted;
}

// 让 teaching_classes.enrolled_count 与 enrollments 实际记录保持一致,
// 避免冗余计数漂移(选课/退课后调用)。
int TeachingRepository::sync_enrolled_counts() {
    return static_cast<int>(db_.execute(
            "UPDATE teaching_classes tc SET tc.enrolled_count = ("
            "  SELECT COUNT(*) FROM enrollments e WHERE e.teaching_class_id = tc.id "
            "  AND e.status IN ('enrolled','completed'))"));
}

// --------------------------------------------------------------- 审计
long long TeachingRepository::insert_run(const SchedulingRunRecord& record) {
    db_.execute(
            "INSERT INTO scheduling_runs (semester_code, algorithm, started_at, finished_at, "
            "duration_ms, total_courses, total_classes, scheduled_entries, failed_classes, "
            "status, message) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            {record.semester, record.algorithm, record.started_at, record.finished_at,
             std::to_string(record.duration_ms), std::to_string(record.total_courses),
             std::to_string(record.total_classes), std::to_string(record.scheduled_entries),
             std::to_string(record.failed_classes), record.status, record.message});
    const db::ResultSet result = db_.query("SELECT LAST_INSERT_ID() AS id");
    return result.empty() ? 0 : result.first().get_long("id");
}

std::vector<SchedulingRunRecord> TeachingRepository::recent_runs(int limit) const {
    const db::ResultSet result = db_.query(
            "SELECT id, semester_code, algorithm, "
            "DATE_FORMAT(started_at, '%Y-%m-%d %H:%i:%s') AS started_at, "
            "DATE_FORMAT(finished_at, '%Y-%m-%d %H:%i:%s') AS finished_at, duration_ms, "
            "total_courses, total_classes, scheduled_entries, failed_classes, status, message "
            "FROM scheduling_runs ORDER BY id DESC LIMIT " + std::to_string(std::max(1, limit)));
    std::vector<SchedulingRunRecord> list;
    for (const db::Row& row : result.rows()) list.push_back(run_from_row(row));
    return list;
}

}  // namespace scheduler::repo
