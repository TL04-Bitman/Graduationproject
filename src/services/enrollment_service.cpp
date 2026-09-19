// ============================================================================
//  services/enrollment_service.cpp —— 学生端选课/退课/查课 实现
// ============================================================================
#include "scheduler/services/enrollment_service.hpp"

#include <algorithm>
#include <map>
#include <string>

#include "scheduler/config.hpp"
#include "scheduler/exceptions.hpp"
#include "scheduler/rules.hpp"
#include "scheduler/util/str.hpp"

namespace scheduler::service {
namespace {

using rules::OccupiedSlot;
using rules::Slot;

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

// 汇总某学生在本学期已选课程占用的时段(时间冲突判定用)
std::vector<OccupiedSlot> collect_occupied_slots(repo::StudentRepository& students,
                                                 repo::TeachingRepository& teaching,
                                                 int student_id,
                                                 const std::string& semester) {
    std::vector<OccupiedSlot> occupied;
    for (const Enrollment& enrollment : students.enrollments_of_semester(student_id, semester)) {
        if (enrollment.status == EnrollmentStatus::Dropped) continue;
        const std::optional<TeachingClass> teaching_class =
                teaching.class_by_id(enrollment.teaching_class_id);
        const std::string class_name =
                teaching_class ? teaching_class->name : std::to_string(enrollment.teaching_class_id);
        for (const ScheduleEntry& entry : teaching.schedule_of_class(enrollment.teaching_class_id)) {
            occupied.push_back({{entry.day_of_week, entry.period_no}, class_name,
                                teaching_class ? teaching_class->code : ""});
        }
    }
    return occupied;
}

// 某教学班的时段描述(如 "星期一 · 第 1 大节、星期三 · 第 3 大节")
std::string describe_slots(const std::vector<ScheduleEntry>& schedule) {
    std::vector<std::string> texts;
    for (const ScheduleEntry& entry : schedule) {
        texts.push_back(rules::slot_text(Slot{entry.day_of_week, entry.period_no}));
    }
    return str::join(texts, "、");
}

}  // namespace

std::string EnrollmentService::resolve_semester(const std::string& semester) const {
    return semester.empty() ? app_config().current_semester : semester;
}

Student EnrollmentService::require_student(const std::string& student_no) {
    const std::optional<Student> student = students_.student_by_no(student_no);
    if (!student) throw NotFoundError("学号不存在: " + student_no);
    return *student;
}

TeachingClass EnrollmentService::require_class(const std::string& class_code) {
    const std::optional<TeachingClass> teaching_class = teaching_.class_by_code(class_code);
    if (!teaching_class) throw NotFoundError("教学班不存在: " + class_code);
    return *teaching_class;
}

// ---------------------------------------------------------------------------
//  登录: 口令摘要由 MySQL 侧计算(SHA2), 避免在 C++ 里重复实现哈希算法,
//        同时保证"口令永不以明文形式到达业务层"。
// ---------------------------------------------------------------------------
std::optional<Student> EnrollmentService::login(const std::string& student_no,
                                               const std::string& password) {
    const db::ResultSet result = db_.query(
            "SELECT id, student_no, student_name, gender, grade_year, major, admin_class, dept, "
            "status, (password_hash = SHA2(CONCAT(student_no, '#', ?), 256)) AS matched "
            "FROM students WHERE student_no = ?",
            {password, student_no});
    if (result.empty()) return std::nullopt;
    if (result.first().get_int("matched", 0) != 1) return std::nullopt;
    return student_from_row(result.first());
}

double EnrollmentService::credits_of(const std::string& student_no, const std::string& semester) {
    const Student student = require_student(student_no);
    return students_.credits_of_semester(student.id, resolve_semester(semester));
}

// ---------------------------------------------------------------------------
//  可选课程列表: 逐条给出"能不能选 + 为什么不能选", 这是学生端最主要的交互入口
// ---------------------------------------------------------------------------
std::vector<EnrollmentService::ClassOption> EnrollmentService::class_options(
        const std::string& student_no, std::string semester) {
    const Config& config = app_config();
    const std::string term = resolve_semester(semester);
    const Student student = require_student(student_no);

    const std::map<int, Course> courses = catalog_.course_map(true);
    const std::map<int, Teacher> teachers = catalog_.teacher_map();
    const std::map<int, Classroom> classrooms = catalog_.classroom_map();
    const std::set<std::string> passed =
            students_.passed_course_codes(student.id, config.min_score_to_pass);
    const std::vector<rules::OccupiedSlot> occupied =
            collect_occupied_slots(students_, teaching_, student.id, term);
    const double used_credits = students_.credits_of_semester(student.id, term);

    // 一次性取回本学期全部排课结果并建立索引, 避免逐个教学班查询(N+1 问题)
    std::map<int, std::vector<ScheduleEntry>> schedule_map;
    for (const ScheduleEntry& entry : teaching_.schedule_of_semester(term)) {
        schedule_map[entry.teaching_class_id].push_back(entry);
    }

    std::vector<ClassOption> options;
    for (const TeachingClass& teaching_class : teaching_.classes_of_semester(term)) {
        const auto course_it = courses.find(teaching_class.course_id);
        if (course_it == courses.end()) continue;
        const Course& course = course_it->second;

        ClassOption option;
        option.teaching_class = teaching_class;
        option.course = course;
        const auto teacher_it = teachers.find(teaching_class.teacher_id);
        if (teacher_it != teachers.end()) option.teacher = teacher_it->second;

        const std::vector<ScheduleEntry>& schedule = schedule_map[teaching_class.id];
        option.slot_text = schedule.empty() ? "尚未排课" : describe_slots(schedule);

        std::vector<std::string> room_texts;
        room_texts.reserve(schedule.size());
        for (const ScheduleEntry& entry : schedule) {
            const auto room_it = classrooms.find(entry.classroom_id);
            room_texts.push_back(room_it == classrooms.end() ? "-" : room_it->second.code);
        }
        option.room_text = room_texts.empty() ? "-" : str::join(room_texts, "、");

        // 先修校验与时间冲突(明细保留下来, 界面可以直接展示原因)
        const std::vector<std::string> prereq_codes = catalog_.prereq_codes_of(course.code);
        option.missing_prerequisites = rules::missing_prerequisites(prereq_codes, passed);
        for (const ScheduleEntry& entry : schedule) {
            const std::vector<rules::ConflictInfo> conflicts = rules::find_time_conflicts(
                    {rules::Slot{entry.day_of_week, entry.period_no}}, course.name, occupied);
            for (const rules::ConflictInfo& conflict : conflicts) {
                option.conflicts.push_back(conflict.conflicting_course_name + "(" +
                                           rules::slot_text(conflict.slot) + ")");
            }
        }

        // 状态判定顺序即"阻断优先级": 越靠前的条件越是不可协商的硬约束
        const bool already = students_.is_enrolled(student.id, teaching_class.id, term);
        if (!teaching_class.open_for_enroll) {
            option.status = "未开放选课";
        } else if (schedule.empty() && config.require_scheduled_before_enroll) {
            option.status = "尚未排课";
        } else if (already) {
            option.status = "已选";
        } else if (!option.missing_prerequisites.empty()) {
            option.status = "先修未满足";
        } else if (rules::exceeds_credit_limit(used_credits, course.credits,
                                               config.max_credits_per_semester)) {
            option.status = "超出学分上限";
        } else if (students_.enrolled_count(teaching_class.id) >= teaching_class.capacity) {
            option.status = "容量已满";
        } else if (!option.conflicts.empty()) {
            option.status = "时间冲突";
        } else {
            option.status = "可选";
            option.selectable = true;
        }
        options.push_back(std::move(option));
    }

    std::sort(options.begin(), options.end(), [](const ClassOption& lhs, const ClassOption& rhs) {
        if (lhs.selectable != rhs.selectable) return lhs.selectable;   // 可选的排前面
        return lhs.course.code < rhs.course.code;
    });
    return options;
}

// ---------------------------------------------------------------------------
//  ★ 选课: 七道校验全部通过后才写入, 失败时抛出携带明细的业务异常
// ---------------------------------------------------------------------------
EnrollmentService::EnrollResult EnrollmentService::enroll(const std::string& student_no,
                                                         const std::string& class_code,
                                                         std::string semester) {
    const Config& config = app_config();
    const std::string term = resolve_semester(semester);
    const Student student = require_student(student_no);

    // ① 学籍状态
    if (student.status != "active") {
        throw EnrollmentClosedError("当前学籍状态为 " + student.status + ", 不能选课");
    }

    // ② 教学班是否可选
    const TeachingClass teaching_class = require_class(class_code);
    if (teaching_class.semester != term) {
        throw SchedulerError("教学班 " + class_code + " 属于学期 " + teaching_class.semester +
                             ", 与目标学期 " + term + " 不一致");
    }
    if (!teaching_class.open_for_enroll) {
        throw EnrollmentClosedError("教学班 " + class_code + " 未开放选课");
    }
    const std::optional<Course> course = catalog_.course_by_id(teaching_class.course_id);
    if (!course) throw NotFoundError("教学班对应的课程不存在: " + class_code);

    // ③ 幂等保护: 不允许重复选择同一教学班
    if (students_.is_enrolled(student.id, teaching_class.id, term)) {
        throw SchedulerError("你已选中该教学班: " + class_code);
    }

    const std::vector<ScheduleEntry> schedule = teaching_.schedule_of_class(teaching_class.id);
    if (config.require_scheduled_before_enroll && schedule.empty()) {
        throw NotScheduledError("教学班 " + class_code +
                                " 尚未完成排课, 请等待教务完成本学期排课后再选课");
    }

    // ④ 先修课程校验(先修关系来自 DAG, 判定逻辑复用 rules::missing_prerequisites)
    const std::vector<std::string> prereq_codes = catalog_.prereq_codes_of(course->code);
    const std::set<std::string> passed =
            students_.passed_course_codes(student.id, config.min_score_to_pass);
    const std::vector<std::string> missing = rules::missing_prerequisites(prereq_codes, passed);
    if (!missing.empty()) {
        throw PrerequisiteError("先修课程未通过: " + str::join(missing, "、") + "(需成绩 ≥ " +
                                        std::to_string(config.min_score_to_pass) + " 分)",
                                missing);
    }

    // ⑤ 学期学分上限
    const double used_credits = students_.credits_of_semester(student.id, term);
    if (rules::exceeds_credit_limit(used_credits, course->credits,
                                    config.max_credits_per_semester)) {
        throw CreditLimitError("超出学期学分上限: 已选 " + str::number(used_credits, 1) +
                                       " 学分, 本课程 " + str::number(course->credits, 1) +
                                       " 学分, 上限 " +
                                       str::number(config.max_credits_per_semester, 1) + " 学分",
                               used_credits, config.max_credits_per_semester);
    }

    // ⑥ 容量
    const int enrolled = students_.enrolled_count(teaching_class.id);
    if (enrolled >= teaching_class.capacity) {
        throw CapacityError("教学班 " + class_code + " 已满(" + std::to_string(enrolled) + "/" +
                            std::to_string(teaching_class.capacity) + ")");
    }

    // ⑦ 时间冲突: 与本人本学期已选课程逐一比对时段
    std::vector<rules::Slot> target_slots;
    target_slots.reserve(schedule.size());
    for (const ScheduleEntry& entry : schedule) {
        target_slots.push_back(rules::Slot{entry.day_of_week, entry.period_no});
    }
    const std::vector<rules::OccupiedSlot> occupied =
            collect_occupied_slots(students_, teaching_, student.id, term);
    const std::vector<rules::ConflictInfo> conflicts =
            rules::find_time_conflicts(target_slots, course->name, occupied);
    if (!conflicts.empty()) {
        std::vector<std::string> details;
        for (const rules::ConflictInfo& conflict : conflicts) {
            details.push_back(conflict.conflicting_course_name + "(" +
                              rules::slot_text(conflict.slot) + ")");
        }
        throw TimeConflictError("与已选课程时间冲突: " + str::join(details, "、"), details);
    }

    // ⑧ 事务写入: 选课记录 + 班级人数冗余字段
    double credits_after = used_credits;
    db_.transaction([&](db::Database& transaction) {
        repo::StudentRepository transaction_students(transaction);
        transaction_students.insert_enrollment(student.id, teaching_class.id, term);
        transaction_students.refresh_class_enrolled(teaching_class.id);
        credits_after = transaction_students.credits_of_semester(student.id, term);
    });

    const std::string slots_text = schedule.empty() ? "尚未排课" : describe_slots(schedule);
    return {student.name + " 成功选修《" + course->name + "》(" + class_code + "), 上课时间: " +
                    slots_text + ", 当前学期累计 " + str::number(credits_after, 1) + " 学分",
            teaching_class, credits_after};
}

// ---------------------------------------------------------------------------
//  退课: 软删除(status = dropped), 保留历史便于审计与成绩追溯
// ---------------------------------------------------------------------------
EnrollmentService::DropResult EnrollmentService::drop(const std::string& student_no,
                                                     const std::string& class_code,
                                                     std::string semester) {
    const std::string term = resolve_semester(semester);
    const Student student = require_student(student_no);
    const TeachingClass teaching_class = require_class(class_code);

    if (!students_.drop_enrollment(student.id, class_code, term)) {
        throw NotFoundError("没有找到可退选的记录: " + class_code + "(可能未选或已退课)");
    }
    students_.refresh_class_enrolled(teaching_class.id);
    const double credits_after = students_.credits_of_semester(student.id, term);
    return {"已退选 " + class_code + ", 当前学期累计 " + str::number(credits_after, 1) + " 学分",
            credits_after};
}

// ---------------------------------------------------------------------------
//  我的课表(查课)
// ---------------------------------------------------------------------------
EnrollmentService::TimetableResult EnrollmentService::timetable(const std::string& student_no,
                                                               std::string semester) {
    const std::string term = resolve_semester(semester);
    TimetableResult result;
    result.student = require_student(student_no);
    result.semester = term;
    result.items = students_.timetable(result.student.id, term);
    for (const TimetableItem& item : result.items) {
        result.total_credits += item.credits;
        if (item.day_of_week > 0) {
            ++result.scheduled_count;
        } else {
            result.unscheduled.push_back(item.course_code + " " + item.course_name);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
//  先修进度: 面向学生的"我想修这门课还差哪些先修"
// ---------------------------------------------------------------------------
std::vector<EnrollmentService::PrereqProgress> EnrollmentService::prereq_progress(
        const std::string& student_no) {
    const Config& config = app_config();
    const Student student = require_student(student_no);
    const std::set<std::string> passed =
            students_.passed_course_codes(student.id, config.min_score_to_pass);

    std::vector<PrereqProgress> progress_list;
    for (const Course& course : catalog_.courses(true)) {
        const std::vector<std::string> prereq_codes = catalog_.prereq_codes_of(course.code);
        if (prereq_codes.empty()) continue;   // 没有先修要求的课程无需展示
        PrereqProgress progress;
        progress.course_code = course.code;
        progress.course_name = course.name;
        progress.credits = course.credits;
        progress.prerequisites = prereq_codes;
        for (const std::string& code : prereq_codes) {
            if (passed.find(code) != passed.end()) {
                progress.satisfied.push_back(code);
            } else {
                progress.missing.push_back(code);
            }
        }
        progress_list.push_back(std::move(progress));
    }
    return progress_list;
}

}  // namespace scheduler::service
