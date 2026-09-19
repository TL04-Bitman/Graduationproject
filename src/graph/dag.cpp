// ============================================================================
//  graph/dag.cpp —— DAG 与 Kahn 拓扑排序实现(项目核心算法)
// ============================================================================
#include "scheduler/graph/dag.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <tuple>

#include "scheduler/util/str.hpp"

namespace scheduler::graph {
namespace {

// 边去重用的 64 位键(顶点 id 为正整数, 高 32 位放起点)
long long edge_key(int from, int to) {
    return (static_cast<long long>(from) << 32) | static_cast<unsigned int>(to);
}

}  // namespace

// ---------------------------------------------------------------- 图构建
void CourseDAG::add_vertex(Vertex vertex) {
    const int id = vertex.id;
    if (id <= 0) throw SchedulerError("DAG 顶点 id 必须为正整数");
    if (vertices_.find(id) != vertices_.end()) return;   // 幂等: 重复添加相同顶点直接忽略
    vertices_.emplace(id, std::move(vertex));
    successors_[id];
    predecessors_[id];
    vertex_ids_.push_back(id);
}

// 添加一条"先修课 -> 后续课"的有向边。
// 注意: 这里不拦截自环(A 以 A 为先修), 因为自环本身就是一个长度为 1 的环,
//       应当由拓扑排序检测出来并报错; 数据库层另有 CHECK 约束做第一道防线。
void CourseDAG::add_edge(int prereq_course_id, int course_id) {
    ensure_vertex(prereq_course_id);
    ensure_vertex(course_id);
    if (!edge_set_.insert(edge_key(prereq_course_id, course_id)).second) return;  // 去重
    successors_[prereq_course_id].push_back(course_id);
    predecessors_[course_id].push_back(prereq_course_id);
    ++edge_count_;
}

void CourseDAG::ensure_vertex(int id) const {
    if (vertices_.find(id) == vertices_.end()) {
        throw SchedulerError("DAG 中不存在顶点 id=" + std::to_string(id) +
                             "(添加边前必须先添加顶点)");
    }
}

bool CourseDAG::has_vertex(int id) const { return vertices_.find(id) != vertices_.end(); }

const Vertex& CourseDAG::vertex(int id) const {
    auto it = vertices_.find(id);
    if (it == vertices_.end()) throw NotFoundError("DAG 中不存在顶点 id=" + std::to_string(id));
    return it->second;
}

const std::vector<int>& CourseDAG::successors(int id) const {
    auto it = successors_.find(id);
    if (it == successors_.end()) throw NotFoundError("DAG 中不存在顶点 id=" + std::to_string(id));
    return it->second;
}

const std::vector<int>& CourseDAG::predecessors(int id) const {
    auto it = predecessors_.find(id);
    if (it == predecessors_.end()) throw NotFoundError("DAG 中不存在顶点 id=" + std::to_string(id));
    return it->second;
}

// ------------------------------------------------------- TopologicalResult
bool TopologicalResult::satisfies(const std::vector<std::pair<int, int>>& edges) const {
    if (has_cycle()) return false;
    for (const auto& [from, to] : edges) {
        const int pos_from = position_of(from);
        const int pos_to = position_of(to);
        if (pos_from < 0 || pos_to < 0) return false;
        if (pos_from >= pos_to) return false;   // 先修课必须排在后继课程之前
    }
    return true;
}

int TopologicalResult::position_of(int node) const {
    auto it = position.find(node);
    return it == position.end() ? -1 : it->second;
}

// ===========================================================================
//  ★ 核心算法: Kahn 拓扑排序
//
//  步骤 1  统计每个顶点的入度: indegree[v] = |{先修课 of v}|
//  步骤 2  所有入度为 0 的顶点(没有先修课或先修课已排完)进入就绪队列
//  步骤 3  循环: 取出队首顶点 v -> 追加到结果序列 -> 对 v 的每个后继 s
//          执行 indegree[s] -= 1, 若减到 0 则把 s 入队
//  步骤 4  队列空时结束。此时:
//              结果序列长度 == 顶点总数  => 图无环, 排序成功
//              结果序列长度 != 顶点总数  => 剩余顶点入度恒 > 0, 存在循环依赖
//
//  复杂度: O(V + E)。就绪队列用 std::priority_queue 实现, 只是为了让"同样入度
//  为 0 时先排谁"变得可控(稳定、可按先修层级优先), 并不影响拓扑序的合法性 ——
//  因为任何时刻都只有入度为 0 的顶点才允许出队。
// ===========================================================================
TopologicalResult CourseDAG::topological_sort(Priority priority) const {
    TopologicalResult result;
    result.total = vertices_.size();
    if (vertices_.empty()) return result;

    // ---- 步骤 1: 初始入度 ----
    std::unordered_map<int, int> indegree;
    indegree.reserve(vertices_.size() * 2);
    for (int id : vertex_ids_) {
        indegree[id] = static_cast<int>(predecessors_.at(id).size());
    }
    result.indegree_initial = indegree;

    // 先修层级总是计算并返回: 层级越小说明基础越前置, 排课时越应先占用教学资源。
    // 计算成本 O(V+E), 与拓扑排序同阶, 不会成为瓶颈。
    result.levels = compute_levels();

    // ---- 步骤 2: 就绪队列(三种排序策略, 保证结果可复现) ----
    long long counter = 0;
    std::unordered_map<int, long long> enqueue_seq;
    auto priority_key = [&](int id) -> std::tuple<long long, int, std::string> {
        switch (priority) {
            case Priority::Fifo:
                return {enqueue_seq[id], 0, ""};
            case Priority::StableByKey:
                return {0, 0, key_of(id)};
            case Priority::LevelThenKey: {
                auto it = result.levels.find(id);
                const int level = it == result.levels.end() ? 0 : it->second;
                return {0, level, key_of(id)};
            }
        }
        return {0, 0, ""};
    };
    // std::priority_queue 默认大顶堆, 这里用 ">" 反转成小顶堆(优先级键最小者先出)
    auto comparator = [&](int lhs, int rhs) { return priority_key(lhs) > priority_key(rhs); };
    std::priority_queue<int, std::vector<int>, decltype(comparator)> ready(comparator);

    for (int id : vertex_ids_) {
        if (indegree[id] == 0) {
            enqueue_seq[id] = counter++;
            ready.push(id);
        }
    }

    // ---- 步骤 3: 不断取出入度为 0 的顶点, 松弛其后继的入度 ----
    while (!ready.empty()) {
        const int node = ready.top();
        ready.pop();
        result.position[node] = static_cast<int>(result.order.size());
        result.order.push_back(node);

        // 后继按课程编号处理, 进一步保证同一入度批次的输出顺序稳定
        std::vector<int> nexts = successors_.at(node);
        std::sort(nexts.begin(), nexts.end(),
                  [&](int a, int b) { return key_of(a) < key_of(b); });
        for (int succ : nexts) {
            indegree[succ] -= 1;
            if (indegree[succ] == 0) {
                enqueue_seq[succ] = counter++;
                ready.push(succ);
            }
        }
    }

    // ---- 步骤 4: 判环 ----
    result.acyclic = (result.order.size() == vertices_.size());
    if (!result.acyclic) {
        for (int id : vertex_ids_) {
            if (indegree[id] > 0) result.pending.push_back(id);   // 入度永不归零的顶点
        }
        result.cycle_path = find_cycle_path();                    // 定位一个具体环路
    }
    return result;
}

// 排课/教学计划等业务动作都要求先修图必须是 DAG:
// 一旦拓扑序列长度 != 课程总数, 直接抛出 CycleDetectedError 终止流程。
TopologicalResult CourseDAG::require_acyclic(Priority priority) const {
    TopologicalResult result = topological_sort(priority);
    if (result.has_cycle()) {
        CycleDetectedError::Info info;
        info.cycle_path = keys_of(result.cycle_path);
        info.pending_codes = keys_of(result.pending);
        info.sorted_count = result.sorted_count();
        info.total_count = result.total;
        throw CycleDetectedError(
                "课程先修关系存在循环依赖, 无法生成合法的教学顺序, 已终止本次操作", std::move(info));
    }
    return result;
}

// ===========================================================================
//  找出一个具体环路: 迭代式 DFS + 三色标记(白=未访问, 灰=在当前搜索栈上, 黑=已完成)
//  顺着有向边走, 若遇到"灰色"顶点, 说明这条路径绕回了仍在搜索栈上的祖先, 即
//  发现回边; 环就是搜索栈上从该祖先到栈顶的一段(再补上祖先自身闭合)。
//  使用显式栈而非递归, 避免课程图很深(或恶意数据)时爆栈。
// ===========================================================================
std::vector<int> CourseDAG::find_cycle_path() const {
    enum Color { White = 0, Gray = 1, Black = 2 };
    std::unordered_map<int, int> color;
    color.reserve(vertices_.size() * 2);
    for (int id : vertex_ids_) color[id] = White;

    std::vector<int> stack_nodes;
    std::vector<std::size_t> stack_index;   // 每个栈帧当前遍历到第几条出边

    for (int start : vertex_ids_) {
        if (color[start] != White) continue;
        color[start] = Gray;
        stack_nodes.assign(1, start);
        stack_index.assign(1, 0);
        while (!stack_nodes.empty()) {
            const int node = stack_nodes.back();
            const std::size_t index = stack_index.back();
            const std::vector<int>& nexts = successors_.at(node);
            if (index < nexts.size()) {
                stack_index.back() = index + 1;
                const int next = nexts[index];
                if (color[next] == Gray) {
                    // 回边 -> 构造环路: next -> ... -> node -> next
                    std::vector<int> path;
                    auto it = std::find(stack_nodes.begin(), stack_nodes.end(), next);
                    path.assign(it, stack_nodes.end());
                    path.push_back(next);
                    return path;
                }
                if (color[next] == White) {
                    color[next] = Gray;
                    stack_nodes.push_back(next);
                    stack_index.push_back(0);
                }
            } else {
                color[node] = Black;
                stack_nodes.pop_back();
                stack_index.pop_back();
            }
        }
    }
    return {};   // 图中没有环
}

// ===========================================================================
//  先修层级: 层 0 = 没有先修课; 其余 = max(先修课层级) + 1, 即最长路径深度。
//  实现上借助"按入度推进的 BFS"(与 Kahn 同族): 只有先修课全部处理完的顶点
//  才会出队, 因此计算 level 时其先修课层级一定已是最终值。
//  若图中有环, 环上顶点永远不会出队, 层级保持部分累积值 —— 不会死循环。
// ===========================================================================
std::unordered_map<int, int> CourseDAG::compute_levels() const {
    std::unordered_map<int, int> indegree;
    std::unordered_map<int, int> levels;
    indegree.reserve(vertices_.size() * 2);
    levels.reserve(vertices_.size() * 2);
    std::queue<int> ready;
    for (int id : vertex_ids_) {
        indegree[id] = static_cast<int>(predecessors_.at(id).size());
        levels[id] = 0;
        if (indegree[id] == 0) ready.push(id);
    }
    while (!ready.empty()) {
        const int node = ready.front();
        ready.pop();
        for (int succ : successors_.at(node)) {
            levels[succ] = std::max(levels[succ], levels[node] + 1);
            if (--indegree[succ] == 0) ready.push(succ);
        }
    }
    return levels;
}

std::vector<std::vector<int>> CourseDAG::level_groups() const {
    const std::unordered_map<int, int> levels = compute_levels();
    int max_level = -1;
    for (const auto& entry : levels) {
        max_level = std::max(max_level, entry.second);
    }
    std::vector<std::vector<int>> groups(static_cast<std::size_t>(max_level + 1));
    for (int id : vertex_ids_) {
        auto it = levels.find(id);
        const int level = it == levels.end() ? 0 : it->second;
        groups[static_cast<std::size_t>(level)].push_back(id);
    }
    for (auto& group : groups) {
        std::sort(group.begin(), group.end(), [&](int a, int b) { return key_of(a) < key_of(b); });
    }
    return groups;
}

std::vector<int> CourseDAG::roots() const {
    std::vector<int> result;
    for (int id : vertex_ids_) {
        if (predecessors_.at(id).empty()) result.push_back(id);
    }
    return result;
}

std::vector<int> CourseDAG::leaves() const {
    std::vector<int> result;
    for (int id : vertex_ids_) {
        if (successors_.at(id).empty()) result.push_back(id);
    }
    return result;
}

std::string CourseDAG::key_of(int id) const {
    auto it = vertices_.find(id);
    return it == vertices_.end() ? ("#" + std::to_string(id)) : it->second.key;
}

std::string CourseDAG::describe_path(const std::vector<int>& path) const {
    std::string text;
    for (std::size_t i = 0; i < path.size(); ++i) {
        text += (i ? " → " : "") + key_of(path[i]);
    }
    return text;
}

std::vector<std::string> CourseDAG::keys_of(const std::vector<int>& ids) const {
    std::vector<std::string> keys;
    keys.reserve(ids.size());
    for (int id : ids) keys.push_back(key_of(id));
    return keys;
}

}  // namespace scheduler::graph
