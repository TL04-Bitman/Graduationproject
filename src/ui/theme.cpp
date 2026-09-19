// ============================================================================
//  ui/theme.cpp —— 终端配色与组件实现
// ============================================================================
#include "scheduler/ui/theme.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>

#include "scheduler/config.hpp"
#include "scheduler/util/str.hpp"

namespace scheduler::ui {
namespace {

const char* code_of(Color color) {
    switch (color) {
        case Color::Reset:   return "\033[0m";
        case Color::Bold:    return "\033[1m";
        case Color::Dim:     return "\033[2m";
        case Color::Red:     return "\033[31m";
        case Color::Green:   return "\033[32m";
        case Color::Yellow:  return "\033[33m";
        case Color::Blue:    return "\033[34m";
        case Color::Magenta: return "\033[35m";
        case Color::Cyan:    return "\033[36m";
        case Color::White:   return "\033[37m";
        case Color::Grey:    return "\033[90m";
    }
    return "\033[0m";
}

}  // namespace

Theme::Theme() {
    // 颜色开关优先级: 环境变量 NO_COLOR > .env(UI_COLOR) > 标准输出是否为终端
    enabled_ = app_config().color && isatty(STDOUT_FILENO) == 1;
}

const Theme& Theme::instance() {
    static Theme instance;
    return instance;
}

const Theme& theme() { return Theme::instance(); }

std::string Theme::paint(const std::string& text, Color color) const {
    if (!enabled_ || text.empty()) return text;
    return std::string(code_of(color)) + text + code_of(Color::Reset);
}

std::string Theme::ok(const std::string& text) const {
    return paint("✔ " + text, Color::Green);
}
std::string Theme::warn(const std::string& text) const {
    return paint("▲ " + text, Color::Yellow);
}
std::string Theme::error(const std::string& text) const {
    return paint("✘ " + text, Color::Red);
}
std::string Theme::info(const std::string& text) const {
    return paint("ℹ " + text, Color::Cyan);
}
std::string Theme::accent(const std::string& text) const {
    return paint(text, Color::Cyan);
}
std::string Theme::muted(const std::string& text) const {
    return paint(text, Color::Grey);
}
std::string Theme::strong(const std::string& text) const {
    return paint(text, Color::Bold);
}

int Theme::terminal_width() {
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return std::max(80, std::min<int>(size.ws_col, 160));
    }
    return 120;   // 非终端(重定向/管道)时给一个较宽的默认值, 保证课表可读
}

std::string Theme::rule(int width, const std::string& unit) const {
    const int target = width > 0 ? width : terminal_width();
    return paint(str::repeat(unit, target), Color::Grey);
}

std::string Theme::title_bar(const std::string& title, const std::string& subtitle,
                             int width) const {
    const int target = width > 0 ? width : terminal_width();
    std::string text = " " + title;
    if (!subtitle.empty()) text += "  " + subtitle;
    std::string bar = paint(text, Color::Bold);
    std::string line = str::repeat("━", std::max(0, target));
    return "\n" + paint(line, Color::Cyan) + "\n" + bar + "\n" +
           paint(str::repeat("━", std::max(0, target)), Color::Cyan) + "\n";
}

std::string Theme::section(const std::string& title) const {
    return "\n" + paint("▌" + title, Color::Bold) + "\n";
}

std::string Theme::box(const std::string& title, const std::vector<std::string>& lines,
                       Color border) const {
    const int target = std::min(terminal_width(), 100);
    std::size_t content_width = str::display_width(title);
    for (const std::string& line : lines) {
        content_width = std::max<std::size_t>(content_width, str::display_width(line));
    }
    content_width = std::min<std::size_t>(content_width, static_cast<std::size_t>(target - 6));
    const int inner = static_cast<int>(content_width) + 2;

    const std::string horizontal(str::repeat("─", inner));
    std::string output;
    output += paint("┌" + horizontal + "┐", border) + "\n";
    output += paint("│ ", border) + paint(str::pad_display(title, inner - 1), Color::Bold) +
              paint("│", border) + "\n";
    if (!lines.empty()) output += paint("├" + horizontal + "┤", border) + "\n";
    for (const std::string& line : lines) {
        const std::string clipped = str::truncate_display(line, inner - 1);
        output += paint("│ ", border) + str::pad_display(clipped, inner - 1) + paint("│", border) +
                  "\n";
    }
    output += paint("└" + horizontal + "┘", border);
    return output;
}

std::string Theme::success_box(const std::string& title,
                               const std::vector<std::string>& lines) const {
    return box(title, lines, Color::Green);
}
std::string Theme::warning_box(const std::string& title,
                               const std::vector<std::string>& lines) const {
    return box(title, lines, Color::Yellow);
}
std::string Theme::error_box(const std::string& title,
                             const std::vector<std::string>& lines) const {
    return box(title, lines, Color::Red);
}
std::string Theme::info_box(const std::string& title, const std::vector<std::string>& lines) const {
    return box(title, lines, Color::Cyan);
}

std::string Theme::badge(const std::string& text, Color color) const {
    return paint("[" + text + "]", color);
}

}  // namespace scheduler::ui
