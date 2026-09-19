// ============================================================================
//  ui/widgets.cpp —— 终端可视化组件实现
// ============================================================================
#include "scheduler/ui/widgets.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <sstream>

#include "scheduler/util/str.hpp"

namespace scheduler::ui {
namespace {

// 计算列宽: 取"表头宽度"与"该列所有单元格宽度"的较大值, 并受最大宽度限制
std::vector<int> compute_widths(const std::vector<Column>& columns,
                                const std::vector<std::vector<std::string>>& rows) {
    std::vector<int> widths;
    widths.reserve(columns.size());
    for (std::size_t c = 0; c < columns.size(); ++c) {
        int width = str::display_width(columns[c].header);
        for (const std::vector<std::string>& row : rows) {
            if (c < row.size()) width = std::max(width, str::display_width(row[c]));
        }
        width = std::max(width, columns[c].width);
        widths.push_back(width);
    }
    return widths;
}

}  // namespace

// --------------------------------------------------------------------- 表格
Table::Table(std::vector<Column> columns, std::string title)
    : columns_(std::move(columns)), title_(std::move(title)) {}

Table& Table::add_row(std::vector<std::string> cells) {
    rows_.push_back(std::move(cells));
    rule_before_.push_back(false);
    return *this;
}

Table& Table::add_rule() {
    if (!rows_.empty()) rule_before_.back() = true;
    return *this;
}

std::string Table::render() const {
    const int terminal = Theme::terminal_width();
    std::vector<int> widths = compute_widths(columns_, rows_);

    // 若总宽超出终端, 按比例压缩最宽的列(至少保留 8 字符, 超出部分用 … 截断)
    const int chrome = static_cast<int>(columns_.size()) * 3 + 1;
    int total = chrome;
    for (int width : widths) total += width;
    if (total > terminal) {
        int overflow = total - terminal;
        while (overflow > 0) {
            std::size_t widest = 0;
            for (std::size_t c = 1; c < widths.size(); ++c) {
                if (widths[c] > widths[widest]) widest = c;
            }
            if (widths[widest] <= 8) break;
            const int shrink = std::min(overflow, widths[widest] - 8);
            widths[widest] -= shrink;
            overflow -= shrink;
        }
    }

    auto border = [&](const std::string& left, const std::string& middle,
                      const std::string& right) {
        std::string line = left;
        for (std::size_t c = 0; c < widths.size(); ++c) {
            line += str::repeat("─", widths[c] + 2);
            line += (c + 1 == widths.size()) ? right : middle;
        }
        return line;
    };

    std::ostringstream out;
    if (!title_.empty()) {
        out << theme().paint("◆ " + title_, Color::Bold) << "\n";
    }
    out << theme().muted(border("┌", "┬", "┐")) << "\n";
    std::string header_line = "│";
    for (std::size_t c = 0; c < columns_.size(); ++c) {
        header_line += " " + str::pad_display(
                                     str::truncate_display(columns_[c].header, widths[c]),
                                     widths[c]) + " │";
    }
    out << theme().paint(header_line, Color::Bold) << "\n";
    out << theme().muted(border("├", "┼", "┤")) << "\n";

    for (std::size_t r = 0; r < rows_.size(); ++r) {
        if (rule_before_[r]) out << theme().muted(border("├", "┼", "┤")) << "\n";
        std::string line = "│";
        for (std::size_t c = 0; c < columns_.size(); ++c) {
            const std::string cell = c < rows_[r].size() ? rows_[r][c] : std::string();
            line += " " +
                    str::pad_display(str::truncate_display(cell, widths[c]), widths[c],
                                     columns_[c].right_align) +
                    " │";
        }
        out << line << "\n";
    }
    out << theme().muted(border("└", "┴", "┘"));
    return out.str();
}

// ----------------------------------------------------------------- 周课表网格
//  布局: 行 = 大节(含上课时间), 列 = 周一~周五; 每个格子 3 行显示
//        "课程名 / 教师 / 教室", 空时段显示灰色占位符。
std::string render_weekly_grid(const std::vector<std::vector<GridCell>>& grid,
                               const std::vector<std::string>& row_labels, int width) {
    const int terminal = width > 0 ? width : Theme::terminal_width();
    const int days = 5;   // 周一 ~ 周五
    const std::vector<std::string> day_names = {"星期一", "星期二", "星期三", "星期四",
                                                "星期五"};
    const int label_width = 13;
    int cell_width = (terminal - label_width - (days + 1) * 3) / days;
    cell_width = std::max(10, std::min(cell_width, 22));

    auto border = [&](const std::string& left, const std::string& middle,
                      const std::string& right) {
        std::string line = left + str::repeat("─", label_width + 2);
        for (int d = 0; d < days; ++d) {
            line += middle + str::repeat("─", cell_width + 2);
        }
        return line + right;
    };
    auto cell_line = [&](const std::string& text) {
        return " " + str::pad_display(str::truncate_display(text, cell_width), cell_width) + " ";
    };

    std::ostringstream out;
    out << theme().muted(border("┌", "┬", "┐")) << "\n";
    std::string header = "│ " + str::pad_display("节次 / 时间", label_width) + " ";
    for (int d = 0; d < days; ++d) {
        header += "│" + cell_line(theme().strong(day_names[static_cast<std::size_t>(d)]));
    }
    out << header << "│\n";
    out << theme().muted(border("├", "┼", "┤")) << "\n";

    // 每个格子渲染 4 行: 课程名(必要时折成两行) / 教师 / 教室
    for (std::size_t p = 0; p < grid.size(); ++p) {
        const std::vector<std::string> label_parts =
                p < row_labels.size() ? str::split(row_labels[p], '|')
                                      : std::vector<std::string>{};
        // 课程名按显示宽度折行(最多两行), 其余行放教师与教室
        std::vector<std::array<std::string, 4>> cells(static_cast<std::size_t>(days));
        for (int d = 0; d < days; ++d) {
            if (p >= grid.size() || d >= static_cast<int>(grid[p].size())) continue;
            const GridCell& cell = grid[p][static_cast<std::size_t>(d)];
            if (cell.empty) continue;
            const std::vector<std::string> wrapped = str::wrap_display(cell.title, cell_width, 2);
            std::array<std::string, 4> lines{};
            if (!wrapped.empty()) lines[0] = wrapped[0];
            if (wrapped.size() > 1) lines[1] = wrapped[1];
            lines[2] = cell.teacher;
            lines[3] = cell.room;
            cells[static_cast<std::size_t>(d)] = lines;
        }
        for (int line_index = 0; line_index < 4; ++line_index) {
            std::string line = "│ ";
            const std::string label_text =
                    line_index < static_cast<int>(label_parts.size()) ? label_parts[line_index] : "";
            line += str::pad_display(str::truncate_display(label_text, label_width), label_width) +
                    " ";
            for (int d = 0; d < days; ++d) {
                const std::string& raw = cells[static_cast<std::size_t>(d)]
                                              [static_cast<std::size_t>(line_index)];
                const bool filled = !raw.empty();
                const std::string content =
                        filled ? (line_index == 0 ? theme().accent(raw) : raw) : "";
                line += "│" + cell_line(filled ? content
                                               : theme().muted(line_index == 0 ? "—" : ""));
            }
            out << line << "│\n";
        }
        if (p + 1 < grid.size()) out << theme().muted(border("├", "┼", "┤")) << "\n";
    }
    out << theme().muted(border("└", "┴", "┘"));
    return out.str();
}

// ------------------------------------------------------------------- 图表类
std::string progress_bar(double ratio, int width) {
    ratio = std::max(0.0, std::min(1.0, ratio));
    const int filled = static_cast<int>(ratio * width + 0.5);
    const std::string bar = str::repeat("█", filled) + str::repeat("░", width - filled);
    const Color color = ratio >= 0.95 ? Color::Green : (ratio >= 0.6 ? Color::Cyan : Color::Yellow);
    return theme().paint(bar, color);
}

std::string comparison_bars(const std::vector<std::pair<std::string, double>>& items,
                            const std::string& unit, int width) {
    const int terminal = width > 0 ? width : Theme::terminal_width();
    double maximum = 0.0;
    for (const auto& item : items) maximum = std::max(maximum, item.second);
    const int label_width = 24;
    const int bar_width = std::max(10, terminal - label_width - 24);

    std::ostringstream out;
    for (const auto& item : items) {
        const double ratio = maximum <= 0 ? 0.0 : item.second / maximum;
        out << "  "
            << str::pad_display(str::truncate_display(item.first, label_width), label_width)
            << theme().paint(str::repeat("█", std::max(1, static_cast<int>(ratio * bar_width))),
                             Color::Magenta)
            << " " << str::number(item.second, 1) << unit << "\n";
    }
    return out.str();
}

std::string level_flow(const std::vector<std::vector<std::string>>& levels, int width) {
    const int terminal = width > 0 ? width : Theme::terminal_width();
    std::ostringstream out;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        const std::string head = "第 " + std::to_string(i) + " 层";
        const std::string gutter = "        │ ";
        std::string line = "  " + theme().strong(str::pad_display(head, 6)) + " " +
                           theme().paint("│", Color::Cyan) + " ";
        int used = 2 + 6 + 3;
        for (const std::string& code : levels[i]) {
            const int need = str::display_width(code) + 2;
            if (used + need > terminal - 2) {
                out << line << "\n";
                line = gutter;
                used = str::display_width(gutter);
            }
            line += theme().accent(code) + "  ";
            used += need;
        }
        out << line << "\n";
    }
    return out.str();
}

std::string key_value_block(const std::vector<std::pair<std::string, std::string>>& items) {
    std::size_t width = 0;
    for (const auto& item : items) width = std::max(width, static_cast<std::size_t>(str::display_width(item.first)));
    std::ostringstream out;
    for (const auto& item : items) {
        out << "  " << theme().muted(str::pad_display(item.first, static_cast<int>(width))) << " : "
            << item.second << "\n";
    }
    return out.str();
}

std::string bullet_list(const std::vector<std::string>& items, const std::string& marker) {
    std::ostringstream out;
    for (const std::string& item : items) {
        out << "    " << theme().muted(marker) << " " << item << "\n";
    }
    return out.str();
}

// --------------------------------------------------------------------- 交互
void clear_screen() {
    if (theme().enabled()) {
        std::cout << "\033[2J\033[H";
    } else {
        std::cout << "\n";
    }
    std::cout.flush();
}

void pause_prompt(const std::string& hint) {
    std::cout << "\n" << theme().muted(hint + " ...");
    std::cout.flush();
    std::string line;
    std::getline(std::cin, line);
}

int prompt_int(const std::string& prompt, int min_value, int max_value) {
    while (true) {
        std::cout << theme().accent(prompt) << " [" << min_value << "-" << max_value << "]: ";
        std::cout.flush();
        std::string line;
        if (!std::getline(std::cin, line)) return min_value;   // 输入流结束(管道)
        line = str::trim(line);
        if (line.empty()) continue;
        try {
            const int value = std::stoi(line);
            if (value >= min_value && value <= max_value) return value;
        } catch (...) {
            // 落到下面的提示
        }
        std::cout << theme().warn("请输入 " + std::to_string(min_value) + " ~ " +
                                  std::to_string(max_value) + " 之间的数字")
                  << "\n";
    }
}

std::string prompt_text(const std::string& prompt, bool allow_empty) {
    while (true) {
        std::cout << theme().accent(prompt) << " ";
        std::cout.flush();
        std::string line;
        if (!std::getline(std::cin, line)) return std::string();
        line = str::trim(line);
        if (!line.empty() || allow_empty) return line;
        std::cout << theme().warn("输入不能为空, 请重新输入") << "\n";
    }
}

bool prompt_confirm(const std::string& question) {
    std::cout << theme().accent(question) << " [y/N]: ";
    std::cout.flush();
    std::string line;
    if (!std::getline(std::cin, line)) return false;
    line = str::upper(str::trim(line));
    return line == "Y" || line == "YES" || line == "是";
}

}  // namespace scheduler::ui
