// ============================================================================
//  rules.cpp —— 纯函数业务规则实现
// ============================================================================
#include "scheduler/rules.hpp"

#include <algorithm>
#include <map>
#include <utility>

#include "scheduler/util/str.hpp"

namespace scheduler::rules {
namespace {

const std::map<int, std::string> kWeekday = {
        {1, "星期一"}, {2, "星期二"}, {3, "星期三"}, {4, "星期四"},
        {5, "星期五"}, {6, "星期六"}, {7, "星期日"},
};

std::string weekday_text(int day_of_week) {
    auto it = kWeekday.find(day_of_week);
    return it == kWeekday.end() ? ("第" + std::to_string(day_of_week) + "天") : it->second;
}

}  // namespace

std::string slot_text(const Slot& slot) {
    return weekday_text(slot.day_of_week) + " · 第 " + std::to_string(slot.period_no) + " 大节";
}

std::string slot_text(const TimeSlot& slot) {
    return weekday_text(slot.day_of_week) + " · 第 " + std::to_string(slot.period_no) +
           " 大节 (" + slot.start_time.substr(0, 5) + "-" + slot.end_time.substr(0, 5) + ")";
}

// ---------------------------------------------------------------------------
//  规则 1: 先修校验
//  判据: 目标课程的全部先修课都必须在"已通过集合"中; 只要有一门没通过
//        (或修了但不及格), 就返回该课程编号, 由上层阻断选课。
// ---------------------------------------------------------------------------
std::vector<std::string> missing_prerequisites(const std::vector<std::string>& prereq_codes,
                                               const std::set<std::string>& passed_course_codes) {
    std::vector<std::string> missing;
    for (const std::string& code : prereq_codes) {
        if (passed_course_codes.find(code) == passed_course_codes.end()) {
            missing.push_back(code);
        }
    }
    return missing;
}

// ---------------------------------------------------------------------------
//  规则 2: 时间冲突
// ---------------------------------------------------------------------------
std::vector<ConflictInfo> find_time_conflicts(const std::vector<Slot>& target_slots,
                                              const std::string& target_course_name,
                                              const std::vector<OccupiedSlot>& occupied) {
    std::vector<ConflictInfo> conflicts;
    for (const Slot& target : target_slots) {
        for (const OccupiedSlot& item : occupied) {
            if (target == item.slot) {
                conflicts.push_back(
                        {target, target_course_name, item.course_name, item.class_code});
            }
        }
    }
    return conflicts;
}

// ---------------------------------------------------------------------------
//  规则 3: 学分上限
// ---------------------------------------------------------------------------
double remaining_credits(double used_credits, double limit) {
    return limit - used_credits;
}

bool exceeds_credit_limit(double used_credits, double add_credits, double limit) {
    // 加 1e-6 规避浮点误差(学分是 DECIMAL(3,1), 多门累加可能出现 24.999999)
    return used_credits + add_credits > limit + 1e-6;
}

// ---------------------------------------------------------------------------
//  规则 4: 教室排序
//  排序键: (类型是否匹配, 容量浪费量, 教室编号)
//    类型匹配优先: 计算机类课程优先机房/实验室;
//    容量浪费量最小优先: 让大教室留给真正的大班, 提高资源利用率。
// ---------------------------------------------------------------------------
std::vector<const Classroom*> rank_classrooms(const std::vector<Classroom>& rooms,
                                              int need_capacity,
                                              RoomType preferred_type) {
    struct Candidate {
        const Classroom* room;
        int type_rank;
        int waste;
    };
    std::vector<Candidate> candidates;
    for (const Classroom& room : rooms) {
        if (room.capacity < need_capacity) continue;   // 容量不足直接淘汰(硬约束)
        const int type_rank = room.type == preferred_type ? 0 : 1;
        candidates.push_back({&room, type_rank, room.capacity - need_capacity});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& lhs, const Candidate& rhs) {
                  return std::tie(lhs.type_rank, lhs.waste, lhs.room->code) <
                         std::tie(rhs.type_rank, rhs.waste, rhs.room->code);
              });
    std::vector<const Classroom*> ranked;
    ranked.reserve(candidates.size());
    for (const Candidate& candidate : candidates) ranked.push_back(candidate.room);
    return ranked;
}

// ---------------------------------------------------------------------------
//  规则 5: 候选时段排序(已占用时段剔除; 同一天已有课则降级, 避免连排)
// ---------------------------------------------------------------------------
std::vector<Slot> order_candidate_slots(const std::vector<TimeSlot>& all_slots,
                                        const std::set<Slot>& taken_by_class) {
    struct Candidate {
        Slot slot;
        int same_day_penalty;
        int weight;
    };
    std::set<int> busy_days;
    for (const Slot& taken : taken_by_class) busy_days.insert(taken.day_of_week);

    std::vector<Candidate> candidates;
    for (const TimeSlot& slot : all_slots) {
        const Slot candidate{slot.day_of_week, slot.period_no};
        if (taken_by_class.find(candidate) != taken_by_class.end()) continue;   // 本班已占用
        const int penalty = busy_days.count(slot.day_of_week) > 0 ? 1 : 0;
        candidates.push_back({candidate, penalty, slot.weight});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& lhs, const Candidate& rhs) {
                  if (lhs.same_day_penalty != rhs.same_day_penalty) {
                      return lhs.same_day_penalty < rhs.same_day_penalty;
                  }
                  if (lhs.weight != rhs.weight) return lhs.weight < rhs.weight;
                  return lhs.slot < rhs.slot;
              });
    std::vector<Slot> ordered;
    ordered.reserve(candidates.size());
    for (const Candidate& candidate : candidates) ordered.push_back(candidate.slot);
    return ordered;
}

// ---------------------------------------------------------------------------
//  规则 6: 冲突矩阵(哈希键实现, 详见头文件说明)
// ---------------------------------------------------------------------------
void ConflictMatrix::occupy_teacher(int teacher_id, const Slot& slot) {
    teacher_busy_.insert(pack(teacher_id, slot));
    ++teacher_load_[teacher_id];
    ++placements_;
}

void ConflictMatrix::occupy_room(int classroom_id, const Slot& slot) {
    room_busy_.insert(pack(classroom_id, slot));
    ++room_load_[classroom_id];
}

bool ConflictMatrix::teacher_conflict(int teacher_id, const Slot& slot) {
    ++probes_;
    return teacher_busy_.find(pack(teacher_id, slot)) != teacher_busy_.end();
}

bool ConflictMatrix::room_conflict(int classroom_id, const Slot& slot) {
    ++probes_;
    return room_busy_.find(pack(classroom_id, slot)) != room_busy_.end();
}

std::size_t ConflictMatrix::teacher_load(int teacher_id) const {
    auto it = teacher_load_.find(teacher_id);
    return it == teacher_load_.end() ? 0 : it->second;
}

std::size_t ConflictMatrix::room_load(int classroom_id) const {
    auto it = room_load_.find(classroom_id);
    return it == room_load_.end() ? 0 : it->second;
}

}  // namespace scheduler::rules
