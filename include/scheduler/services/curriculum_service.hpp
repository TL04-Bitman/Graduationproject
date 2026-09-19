// ============================================================================
//  services/curriculum_service.hpp —— 课程先修关系(教学计划)服务
//
//  职责:
//    1) 把数据库中的 courses + course_prerequisites 装配成内存 DAG;
//    2) 提供拓扑排序/分层教学计划(哪个学期适合开哪门课);
//    3) ★ 循环依赖检测: 输出序列数 ≠ 课程总数时明确报错, 并给出环路路径;
//    4) 维护先修关系时"先试算后写库": 任何会引入环的编辑都会被拒绝,
//       保证入库的教学计划永远是 DAG。
// ============================================================================
#pragma once

#include <map>
#include <string>
#include <vector>

#include "scheduler/db/database.hpp"
#include "scheduler/graph/dag.hpp"
#include "scheduler/repositories.hpp"

namespace scheduler::service {

class CurriculumService {
public:
    explicit CurriculumService(db::Database& database)
        : db_(database), catalog_(database) {}

    // 装配 DAG(course_index 非空时回填 id -> Course, 便于展示课程名/学分)
    graph::CourseDAG build_dag(std::map<int, Course>* course_index = nullptr) const;

    // 拓扑排序结果(不抛异常, 供"检测/演示"用)
    struct CycleCheckResult {
        bool acyclic = true;
        std::size_t sorted_count = 0;
        std::size_t total_count = 0;
        std::vector<std::string> order_codes;     // 拓扑序列(课程编号)
        std::vector<std::string> cycle_path;      // 环路(有环时非空)
        std::vector<std::string> pending_codes;   // 受阻课程
    };
    CycleCheckResult check_cycle() const;

    // 拓扑排序 + 环校验: 有环抛 CycleDetectedError(排课等关键流程使用)
    graph::TopologicalResult require_plan() const;

    // 分层教学计划: 第 i 层课程列表(层 0 = 无先修课)
    std::vector<std::vector<std::string>> level_plan() const;

    // ★ 新增先修关系(带防环校验)
    //   流程: 在内存图中加入候选边 -> 拓扑排序 -> 若出现环则拒绝写库并抛出
    //         CycleDetectedError; 否则才落库。
    void add_prerequisite(const std::string& course_code, const std::string& prereq_code);
    bool remove_prerequisite(const std::string& course_code, const std::string& prereq_code);

    // 某门课程的先修课程编号
    std::vector<std::string> prereq_codes_of(const std::string& course_code) const;

private:
    db::Database& db_;
    repo::CatalogRepository catalog_;
};

}  // namespace scheduler::service
