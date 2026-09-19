// ============================================================================
//  types.cpp —— 枚举与中文标签的实现
// ============================================================================
#include "scheduler/types.hpp"

#include <map>

namespace scheduler {
namespace {

const std::map<int, std::string> kWeekdayNames = {
        {1, "星期一"}, {2, "星期二"}, {3, "星期三"}, {4, "星期四"},
        {5, "星期五"}, {6, "星期六"}, {7, "星期日"},
};

}  // namespace

std::string to_db_string(CourseType type) {
    switch (type) {
        case CourseType::Required: return "required";
        case CourseType::Elective: return "elective";
        case CourseType::General:  return "general";
    }
    return "required";
}

std::string to_db_string(RoomType type) {
    switch (type) {
        case RoomType::General:  return "general";
        case RoomType::Computer: return "computer";
        case RoomType::Lab:      return "lab";
    }
    return "general";
}

std::string to_db_string(EnrollmentStatus status) {
    switch (status) {
        case EnrollmentStatus::Enrolled:  return "enrolled";
        case EnrollmentStatus::Completed: return "completed";
        case EnrollmentStatus::Failed:    return "failed";
        case EnrollmentStatus::Dropped:   return "dropped";
    }
    return "enrolled";
}

CourseType course_type_from_db(const std::string& value) {
    if (value == "elective") return CourseType::Elective;
    if (value == "general") return CourseType::General;
    return CourseType::Required;
}

RoomType room_type_from_db(const std::string& value) {
    if (value == "computer") return RoomType::Computer;
    if (value == "lab") return RoomType::Lab;
    return RoomType::General;
}

EnrollmentStatus enrollment_status_from_db(const std::string& value) {
    if (value == "completed") return EnrollmentStatus::Completed;
    if (value == "failed") return EnrollmentStatus::Failed;
    if (value == "dropped") return EnrollmentStatus::Dropped;
    return EnrollmentStatus::Enrolled;
}

std::string label(CourseType type) {
    switch (type) {
        case CourseType::Required: return "必修";
        case CourseType::Elective: return "选修";
        case CourseType::General:  return "通识";
    }
    return "必修";
}

std::string label(RoomType type) {
    switch (type) {
        case RoomType::General:  return "普通教室";
        case RoomType::Computer: return "机房";
        case RoomType::Lab:      return "实验室";
    }
    return "普通教室";
}

std::string label(EnrollmentStatus status) {
    switch (status) {
        case EnrollmentStatus::Enrolled:  return "已选";
        case EnrollmentStatus::Completed: return "已完成";
        case EnrollmentStatus::Failed:    return "不及格";
        case EnrollmentStatus::Dropped:   return "已退课";
    }
    return "已选";
}

std::string label(int day_of_week) {
    auto it = kWeekdayNames.find(day_of_week);
    return it == kWeekdayNames.end() ? "未知" : it->second;
}

}  // namespace scheduler
