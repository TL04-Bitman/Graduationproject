// ============================================================================
//  services/curriculum_service.cpp —— 教学计划(DAG)服务实现
// ============================================================================
#include "scheduler/services/curriculum_service.hpp"

#include <algorithm>
#include <utility>

#include "scheduler/exceptions.hpp"
#include "scheduler/util/str.hpp"

namespace scheduler::service {

// 从数据库装配 DAG: 顶点 = 启用状态的课程, 边 = 先修关系(先修课 -> 后续课)
graph::CourseDAG CurriculumService::build_dag(std::map<int, Course>* course_index) const {
    graph::CourseDAG dag;
    for (const Course& course : catalog_.courses(true)) {
        graph::Vertex vertex;
        vertex.id = course.id;
        vertex.key = course.code;
        vertex.name = course.name;
        vertex.credits = course.credits;
        dag.add_vertex(std::move(vertex));
        if (course_index != nullptr) (*course_index)[course.id] = course;
    }
    for (const PrereqEdge& edge : catalog_.prereq_edges()) {
        // 只处理两端都启用的课程, 避免停用课程把图"污染"成有环
        if (dag.has_vertex(edge.prereq_course_id) && dag.has_vertex(edge.course_id)) {
            dag.add_edge(edge.prereq_course_id, edge.course_id);
        }
    }
    return dag;
}

// 循环依赖检测(不抛异常版本): 供界面演示与巡检使用
CurriculumService::CycleCheckResult CurriculumService::check_cycle() const {
    const graph::CourseDAG dag = build_dag();
    const graph::TopologicalResult result = dag.topological_sort(graph::Priority::LevelThenKey);

    CycleCheckResult check;
    check.acyclic = !result.has_cycle();
    check.sorted_count = result.sorted_count();
    check.total_count = result.total;
    check.order_codes = dag.keys_of(result.order);
    check.cycle_path = dag.keys_of(result.cycle_path);
    check.pending_codes = dag.keys_of(result.pending);
    return check;
}

// ★ 关键校验入口: 排课等流程必须先通过它
graph::TopologicalResult CurriculumService::require_plan() const {
    const graph::CourseDAG dag = build_dag();
    return dag.require_acyclic(graph::Priority::LevelThenKey);
}

std::vector<std::vector<std::string>> CurriculumService::level_plan() const {
    const graph::CourseDAG dag = build_dag();
    std::vector<std::vector<std::string>> plan;
    for (const std::vector<int>& group : dag.level_groups()) {
        plan.push_back(dag.keys_of(group));
    }
    return plan;
}

// ---------------------------------------------------------------------------
//  ★ 新增先修关系: 先试算后写库, 保证教学计划永远是 DAG
//    这是"循环依赖检测"能力的第二个应用点: 不仅排课时检查, 维护阶段也拦截。
// ---------------------------------------------------------------------------
void CurriculumService::add_prerequisite(const std::string& course_code,
                                        const std::string& prereq_code) {
    const std::optional<Course> course = catalog_.course_by_code(course_code);
    if (!course) throw NotFoundError("课程不存在: " + course_code);
    const std::optional<Course> prereq = catalog_.course_by_code(prereq_code);
    if (!prereq) throw NotFoundError("先修课程不存在: " + prereq_code);
    if (course_code == prereq_code) {
        throw SchedulerError("课程不能以自身为先修课程(自环必然构成循环依赖)");
    }
    if (catalog_.has_prerequisite(course_code, prereq_code)) {
        throw SchedulerError("该先修关系已存在: " + prereq_code + " → " + course_code);
    }

    std::map<int, Course> course_index;
    graph::CourseDAG dag = build_dag(&course_index);
    dag.add_edge(prereq->id, course->id);   // 候选边只加在内存图里, 尚未落库
    const graph::TopologicalResult result = dag.topological_sort(graph::Priority::LevelThenKey);
    if (result.has_cycle()) {
        CycleDetectedError::Info info;
        info.cycle_path = dag.keys_of(result.cycle_path);
        info.pending_codes = dag.keys_of(result.pending);
        info.sorted_count = result.sorted_count();
        info.total_count = result.total;
        throw CycleDetectedError("新增先修关系 " + prereq_code + " → " + course_code +
                                         " 会造成课程循环依赖, 已拒绝写入",
                                 std::move(info));
    }
    if (!catalog_.add_prerequisite(course_code, prereq_code)) {
        throw DatabaseError("写入先修关系失败: " + prereq_code + " → " + course_code);
    }
}

bool CurriculumService::remove_prerequisite(const std::string& course_code,
                                           const std::string& prereq_code) {
    return catalog_.remove_prerequisite(course_code, prereq_code);
}

std::vector<std::string> CurriculumService::prereq_codes_of(const std::string& course_code) const {
    return catalog_.prereq_codes_of(course_code);
}

}  // namespace scheduler::service
