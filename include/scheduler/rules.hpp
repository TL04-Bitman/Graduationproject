// ============================================================================
//  rules.hpp —— 纯函数业务规则引擎
//
//  为什么单独抽出这一层:
//      排课/选课的核心判断(先修是否满足、时段是否冲突、教室是否合适)本质上是
//      与数据库无关的纯计算。把它们隔离成纯函数后:
//        - 单元测试无需连库即可覆盖全部边界(见 tests/test_rules.cpp);
//        - 排课服务与学生选课服务共享同一套判定逻辑, 不会出现"两处规则不一致";
//        - 后续替换存储层(MySQL -> 其他)时这部分完全不用改。
// ============================================================================
#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "scheduler/types.hpp"

namespace scheduler::rules {

// 一个"星期 + 大节"的组合, 是排课冲突判定的最小粒度
struct Slot {
    int day_of_week = 1;
    int period_no = 1;

    bool operator==(const Slot& other) const {
        return day_of_week == other.day_of_week && period_no == other.period_no;
    }
    bool operator<(const Slot& other) const {
        return std::tie(day_of_week, period_no) < std::tie(other.day_of_week, other.period_no);
    }
};

// "星期一 · 第 3 大节" / 带具体时间
std::string slot_text(const Slot& slot);
std::string slot_text(const TimeSlot& slot);

// ---------------------------------------------------------------------------
//  规则 1: 先修课校验
//  返回尚未通过的先修课程编号列表(空列表表示校验通过)
// ---------------------------------------------------------------------------
std::vector<std::string> missing_prerequisites(const std::vector<std::string>& prereq_codes,
                                               const std::set<std::string>& passed_course_codes);

// ---------------------------------------------------------------------------
//  规则 2: 时间冲突
//  目标教学班的每个时段, 若与学生已选课程占用的时段重合即为冲突
// ---------------------------------------------------------------------------
struct OccupiedSlot {
    Slot slot;
    std::string course_name;
    std::string class_code;
};

struct ConflictInfo {
    Slot slot;
    std::string target_course_name;
    std::string conflicting_course_name;
    std::string conflicting_class_code;
};

std::vector<ConflictInfo> find_time_conflicts(const std::vector<Slot>& target_slots,
                                              const std::string& target_course_name,
                                              const std::vector<OccupiedSlot>& occupied);

// ---------------------------------------------------------------------------
//  规则 3: 学分上限
// ---------------------------------------------------------------------------
double remaining_credits(double used_credits, double limit);
bool exceeds_credit_limit(double used_credits, double add_credits, double limit);

// ---------------------------------------------------------------------------
//  规则 4: 教室选择打分
//  过滤容量不足的教室, 再按"类型匹配 -> 容量贴合 -> 编号"排序。
//  容量贴合的用意: 40 人的班不要占用 120 人的大教室, 优先把大教室留给大班。
// ---------------------------------------------------------------------------
std::vector<const Classroom*> rank_classrooms(const std::vector<Classroom>& rooms,
                                              int need_capacity,
                                              RoomType preferred_type);

// ---------------------------------------------------------------------------
//  规则 5: 候选时段排序
//  已占用时段直接剔除; 同一教学班当天已有课时, 把同一天的其它时段排到后面
//  (避免一门课一天连上两个大节), 其余按 slot_weight(上午优先)排序。
// ---------------------------------------------------------------------------
std::vector<Slot> order_candidate_slots(const std::vector<TimeSlot>& all_slots,
                                        const std::set<Slot>& taken_by_class);

// ---------------------------------------------------------------------------
//  规则 6: 冲突矩阵(排课主循环使用)
//  实现要点: 把 (教师/教室 id, 星期, 节次) 打包成一个 long long 键放进
//  std::unordered_set, 查询是均摊 O(1) 且不产生额外节点分配 ——
//  相比 std::set(红黑树) 或线性扫描, 在大规模排课时常数更小、扩展性更好。
//  probe_count() 统计冲突检测次数, 是效率基准测试的关键指标。
// ---------------------------------------------------------------------------
class ConflictMatrix {
public:
    void occupy_teacher(int teacher_id, const Slot& slot);
    void occupy_room(int classroom_id, const Slot& slot);
    bool teacher_conflict(int teacher_id, const Slot& slot);
    bool room_conflict(int classroom_id, const Slot& slot);

    std::size_t teacher_load(int teacher_id) const;
    std::size_t room_load(int classroom_id) const;
    std::size_t total_placements() const { return placements_; }
    std::size_t probe_count() const { return probes_; }
    void reset_probes() { probes_ = 0; }

private:
    // 打包键: [ id(高位) | 星期 | 节次 ]。id 与节次取值范围都很小, 不会溢出。
    static long long pack(int id, const Slot& slot) {
        return (static_cast<long long>(id) << 20) |
               (static_cast<long long>(slot.day_of_week) << 10) |
               static_cast<long long>(slot.period_no);
    }

    std::unordered_set<long long> teacher_busy_;
    std::unordered_set<long long> room_busy_;
    std::unordered_map<int, std::size_t> teacher_load_;
    std::unordered_map<int, std::size_t> room_load_;
    std::size_t placements_ = 0;
    std::size_t probes_ = 0;
};

}  // namespace scheduler::rules
