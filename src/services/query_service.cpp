// ============================================================================
//  services/query_service.cpp —— 教务端查询统计实现
// ============================================================================
#include "scheduler/services/query_service.hpp"

#include <algorithm>
#include <map>
#include <string>

#include "scheduler/rules.hpp"
#include "scheduler/util/str.hpp"

namespace scheduler::service {

// 全校课表: 每个教学班一行, 多个时段合并展示
std::vector<QueryService::ClassRow> QueryService::class_rows(const std::string& semester) {
    const db::ResultSet result = db_.query(
            "SELECT tc.class_code, c.course_code, c.course_name, t.teacher_name, "
            "tc.enrolled_count, tc.capacity, r.room_code, se.day_of_week, se.period_no "
            "FROM teaching_classes tc "
            "JOIN courses c ON c.id = tc.course_id "
            "JOIN teachers t ON t.id = tc.teacher_id "
            "LEFT JOIN schedule_entries se ON se.teaching_class_id = tc.id "
            "LEFT JOIN classrooms r ON r.id = se.classroom_id "
            "WHERE tc.semester_code = ? "
            "ORDER BY c.course_code, tc.class_code, se.day_of_week, se.period_no",
            {semester});

    std::vector<ClassRow> rows;
    std::map<std::string, std::size_t> index_of;   // class_code -> rows 下标
    for (const db::Row& row : result.rows()) {
        const std::string class_code = row.get("class_code");
        auto it = index_of.find(class_code);
        if (it == index_of.end()) {
            ClassRow item;
            item.class_code = class_code;
            item.course_code = row.get("course_code");
            item.course_name = row.get("course_name");
            item.teacher_name = row.get("teacher_name");
            item.enrolled = row.get_int("enrolled_count");
            item.capacity = row.get_int("capacity");
            rows.push_back(item);
            index_of[class_code] = rows.size() - 1;
            it = index_of.find(class_code);
        }
        ClassRow& item = rows[it->second];
        if (row.has("day_of_week") && !row.is_null("day_of_week")) {
            item.scheduled = true;
            const std::string slot = rules::slot_text(
                    rules::Slot{row.get_int("day_of_week"), row.get_int("period_no")});
            item.slot_text += item.slot_text.empty() ? slot : "、" + slot;
            const std::string room = row.get("room_code");
            if (!room.empty() && item.room_text.find(room) == std::string::npos) {
                item.room_text += item.room_text.empty() ? room : "、" + room;
            }
        }
    }
    return rows;
}

std::vector<QueryService::TeacherLoad> QueryService::teacher_loads(const std::string& semester) {
    const db::ResultSet result = db_.query(
            "SELECT t.teacher_no, t.teacher_name, t.title, t.max_classes, "
            "COUNT(DISTINCT tc.id) AS class_count, COUNT(se.id) AS session_count "
            "FROM teachers t "
            "LEFT JOIN teaching_classes tc ON tc.teacher_id = t.id AND tc.semester_code = ? "
            "LEFT JOIN schedule_entries se ON se.teaching_class_id = tc.id "
            "GROUP BY t.id, t.teacher_no, t.teacher_name, t.title, t.max_classes "
            "ORDER BY session_count DESC, t.teacher_no",
            {semester});
    std::vector<TeacherLoad> list;
    for (const db::Row& row : result.rows()) {
        TeacherLoad load;
        load.teacher_no = row.get("teacher_no");
        load.teacher_name = row.get("teacher_name");
        load.title = row.get("title");
        load.max_classes = row.get_int("max_classes", 4);
        load.class_count = row.get_int("class_count");
        load.session_count = row.get_int("session_count");
        load.overloaded = load.class_count > load.max_classes;
        list.push_back(std::move(load));
    }
    return list;
}

std::vector<QueryService::RoomUsage> QueryService::room_usage(const std::string& semester) {
    const db::ResultSet result = db_.query(
            "SELECT r.room_code, r.building, r.room_type, r.capacity, COUNT(se.id) AS used_slots "
            "FROM classrooms r "
            "LEFT JOIN schedule_entries se ON se.classroom_id = r.id AND se.semester_code = ? "
            "GROUP BY r.id, r.room_code, r.building, r.room_type, r.capacity "
            "ORDER BY r.room_code",
            {semester});
    const std::size_t total_slots = catalog_.time_slots().size();
    std::vector<RoomUsage> list;
    for (const db::Row& row : result.rows()) {
        RoomUsage usage;
        usage.room_code = row.get("room_code");
        usage.building = row.get("building");
        usage.type_text = label(room_type_from_db(row.get("room_type")));
        usage.capacity = row.get_int("capacity");
        usage.used_slots = row.get_int("used_slots");
        usage.total_slots = static_cast<int>(total_slots);
        // 使用率 = 已排时段数 / 全周可用时段数
        usage.usage = total_slots == 0 ? 0.0
                                       : static_cast<double>(usage.used_slots) /
                                                 static_cast<double>(total_slots);
        list.push_back(std::move(usage));
    }
    return list;
}

std::vector<QueryService::StudentRow> QueryService::student_rows(const std::string& semester) {
    const db::ResultSet result = db_.query(
            "SELECT s.student_no, s.student_name, s.major, s.admin_class, s.grade_year, "
            "COUNT(DISTINCT e.teaching_class_id) AS selected_classes, "
            "COALESCE(SUM(c.credits), 0) AS credits "
            "FROM students s "
            "LEFT JOIN enrollments e ON e.student_id = s.id AND e.semester_code = ? "
            "     AND e.status IN ('enrolled','completed') "
            "LEFT JOIN teaching_classes tc ON tc.id = e.teaching_class_id "
            "LEFT JOIN courses c ON c.id = tc.course_id "
            "GROUP BY s.id, s.student_no, s.student_name, s.major, s.admin_class, s.grade_year "
            "ORDER BY s.student_no",
            {semester});
    std::vector<StudentRow> list;
    for (const db::Row& row : result.rows()) {
        StudentRow item;
        item.student_no = row.get("student_no");
        item.student_name = row.get("student_name");
        item.major = row.get("major");
        item.admin_class = row.get("admin_class");
        item.grade_year = row.get_int("grade_year");
        item.selected_classes = row.get_int("selected_classes");
        item.credits = row.get_double("credits");
        list.push_back(std::move(item));
    }
    return list;
}

std::vector<repo::SchedulingRunRecord> QueryService::run_history(int limit) {
    return teaching_.recent_runs(limit);
}

}  // namespace scheduler::service
