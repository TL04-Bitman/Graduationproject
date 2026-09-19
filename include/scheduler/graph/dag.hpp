// ============================================================================
//  graph/dag.hpp —— 课程先修关系的 DAG(有向无环图) 与拓扑排序
//
//  业务背景:
//      "先修课"天然是一个偏序关系: A 是 B 的先修课, 就画一条 A -> B 的边。
//      只要教学计划没有写错, 这张图就是一个 DAG(有向无环图)。
//      教务排课必须先排先修课、再排后续课, 这正是拓扑排序的用武之地;
//      而一旦教学计划里出现循环依赖(例如 A 的先修是 B, B 的先修又是 A),
//      拓扑排序就会"排不完", 于是可以 100% 地发现并定位这个错误。
//
//  算法要点(Kahn 算法, 见 dag.cpp):
//      1) 统计每个顶点的入度;
//      2) 把入度为 0 的顶点放入就绪队列;
//      3) 反复取出队首顶点加入结果序列, 并把它的后继顶点入度减 1,
//         减到 0 的后继进入就绪队列;
//      4) 队列为空时结束。若结果序列长度 != 顶点总数, 则剩下的顶点
//         入度永远无法归零 —— 它们互相等待, 必然构成环。
//      时间复杂度 O(V+E), 空间 O(V+E)。
// ============================================================================
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "scheduler/exceptions.hpp"

namespace scheduler::graph {

// DAG 顶点: 一门课程
struct Vertex {
    int id = 0;
    std::string key;      // 课程编号, 如 CS201(用于稳定排序与界面展示)
    std::string name;     // 课程名称
    double credits = 0.0;
};

// 就绪队列的优先级策略(影响"同样入度为 0 时先排哪门课", 不影响拓扑正确性)
enum class Priority {
    Fifo,           // 先入先出, 最朴素的 Kahn
    StableByKey,    // 按课程编号排序, 输出可复现
    LevelThenKey,   // 先按先修层级(深度)再按课程编号 —— 排课使用该策略
};

// 拓扑排序结果
struct TopologicalResult {
    std::vector<int> order;                        // 拓扑序(顶点 id 序列)
    std::unordered_map<int, int> indegree_initial; // 每个顶点的初始入度
    std::unordered_map<int, int> position;         // 顶点 id -> 拓扑序号
    std::unordered_map<int, int> levels;           // 顶点 id -> 先修层级(最长路径)
    std::vector<int> pending;                      // 未能排序的顶点(有环时非空)
    std::vector<int> cycle_path;                   // 一个具体环路(首尾顶点相同)
    bool acyclic = true;                           // 是否无环
    std::size_t total = 0;                         // 顶点总数

    std::size_t sorted_count() const { return order.size(); }
    bool has_cycle() const { return !acyclic; }
    bool length_equals_total() const { return order.size() == total; }

    // 校验: 对任意边 u->v, 都满足 position[u] < position[v]
    bool satisfies(const std::vector<std::pair<int, int>>& edges) const;
    int position_of(int node) const;   // 不在序列中返回 -1
};

class CourseDAG {
public:
    void add_vertex(Vertex vertex);
    // 添加边: prereq 是 course 的先修课(方向 prereq -> course)
    void add_edge(int prereq_course_id, int course_id);

    bool has_vertex(int id) const;
    const Vertex& vertex(int id) const;
    const std::vector<int>& successors(int id) const;    // 后继课程(需要本课作先修的课)
    const std::vector<int>& predecessors(int id) const;  // 先修课程
    const std::vector<int>& vertex_ids() const { return vertex_ids_; }

    std::size_t vertex_count() const { return vertices_.size(); }
    std::size_t edge_count() const { return edge_count_; }

    // ★ 核心 1: Kahn 拓扑排序。有环时不会抛异常, 而是把结果标记为 acyclic=false
    TopologicalResult topological_sort(Priority priority = Priority::LevelThenKey) const;

    // ★ 核心 2: 拓扑排序 + 环检测。c_序列长度 != 顶点总数 时抛 CycleDetectedError
    TopologicalResult require_acyclic(Priority priority = Priority::LevelThenKey) const;

    // 用迭代式 DFS(三色标记)找出一个具体环路, 返回的序列首尾为同一顶点
    std::vector<int> find_cycle_path() const;

    // 先修层级: 无先修课为 0 层, 其余 = max(先修课层级)+1(最长路径)
    std::unordered_map<int, int> compute_levels() const;

    // 按层级分组(每层内按课程编号排序), 用于"分层教学计划"与排课优先级
    std::vector<std::vector<int>> level_groups() const;

    std::vector<int> roots() const;    // 无先修课的课程
    std::vector<int> leaves() const;   // 不作为任何课程先修的课程

    // 文本工具
    std::string key_of(int id) const;
    std::string describe_path(const std::vector<int>& path) const;   // CS201 → CS204 → CS201
    std::vector<std::string> keys_of(const std::vector<int>& ids) const;

private:
    std::unordered_map<int, Vertex> vertices_;
    std::unordered_map<int, std::vector<int>> successors_;
    std::unordered_map<int, std::vector<int>> predecessors_;
    std::unordered_set<long long> edge_set_;   // 去重: 同一对课程的重复边只保留一条
    std::vector<int> vertex_ids_;              // 顶点插入顺序(保证遍历确定)
    std::size_t edge_count_ = 0;

    // 顶点不存在时抛出的统一异常
    void ensure_vertex(int id) const;
};

}  // namespace scheduler::graph
