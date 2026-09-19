// ============================================================================
//  ui/widgets.hpp —— 终端可视化组件
//
//  包含三类核心组件:
//    1) Table        —— 自适应宽度的表格(正确处理中英文混排对齐);
//    2) 周课表网格    —— 本系统最核心的可视化: "星期 × 大节" 二维课表;
//    3) 图表类组件    —— 进度条、耗时对比条、先修分层流程图;
//  以及统一的交互函数(菜单选择、确认、分页等), 保证界面风格一致。
// ============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "scheduler/ui/theme.hpp"

namespace scheduler::ui {

// --------------------------------------------------------------------- 表格
struct Column {
    std::string header;
    int width = 12;          // 期望显示宽度(中文按 2 计)
    bool right_align = false;
};

class Table {
public:
    Table(std::vector<Column> columns, std::string title = "");
    Table& add_row(std::vector<std::string> cells);
    Table& add_rule();                     // 在下一行前插入分隔线
    const std::vector<std::vector<std::string>>& rows() const { return rows_; }
    std::string render() const;

private:
    std::vector<Column> columns_;
    std::string title_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<bool> rule_before_;
};

// ----------------------------------------------------------------- 周课表网格
struct GridCell {
    std::string title;    // 课程名
    std::string teacher;  // 教师
    std::string room;     // 教室
    std::string tag;      // 教学班号
    bool empty = true;
};

// grid[period_index][day_index], day_index 0=周一 ... 4=周五
std::string render_weekly_grid(const std::vector<std::vector<GridCell>>& grid,
                               const std::vector<std::string>& row_labels,
                               int width = 0);

// ------------------------------------------------------------------- 图表类
std::string progress_bar(double ratio, int width = 24);
// items: {标签, 数值, 单位说明}; 自动按最大值归一化画条形
std::string comparison_bars(const std::vector<std::pair<std::string, double>>& items,
                            const std::string& unit, int width = 0);
// 先修分层流程图: 每层一行, 层内课程并排
std::string level_flow(const std::vector<std::vector<std::string>>& levels, int width = 0);
std::string key_value_block(const std::vector<std::pair<std::string, std::string>>& items);
std::string bullet_list(const std::vector<std::string>& items,
                        const std::string& marker = "•");

// ------------------------------------------------------------------- 交互
void clear_screen();
void pause_prompt(const std::string& hint = "按回车键返回");
int prompt_int(const std::string& prompt, int min_value, int max_value);
std::string prompt_text(const std::string& prompt, bool allow_empty = false);
bool prompt_confirm(const std::string& question);

}  // namespace scheduler::ui
