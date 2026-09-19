// ============================================================================
//  ui/theme.hpp —— 终端配色与语义化输出
//
//  面向"非技术用户快速上手"的设计:
//    - 用颜色 + 图标区分"成功/警告/错误/普通信息", 一眼看清结果;
//    - 所有提示语为中文, 且失败时给出"为什么失败 + 下一步怎么做";
//    - 尊重 NO_COLOR 约定与 .env 中的 UI_COLOR 开关, 重定向到文件时自动关闭颜色。
// ============================================================================
#pragma once

#include <string>
#include <vector>

namespace scheduler::ui {

enum class Color {
    Reset, Bold, Dim,
    Red, Green, Yellow, Blue, Magenta, Cyan, White, Grey,
};

class Theme {
public:
    static const Theme& instance();

    bool enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }

    std::string paint(const std::string& text, Color color) const;

    // 语义化快捷方法
    std::string ok(const std::string& text) const;
    std::string warn(const std::string& text) const;
    std::string error(const std::string& text) const;
    std::string info(const std::string& text) const;
    std::string accent(const std::string& text) const;
    std::string muted(const std::string& text) const;
    std::string strong(const std::string& text) const;

    // 常用组件
    std::string rule(int width = 0, const std::string& unit = "─") const;
    std::string title_bar(const std::string& title, const std::string& subtitle = "",
                          int width = 0) const;
    std::string section(const std::string& title) const;
    std::string box(const std::string& title, const std::vector<std::string>& lines,
                    Color border) const;
    std::string success_box(const std::string& title, const std::vector<std::string>& lines) const;
    std::string warning_box(const std::string& title, const std::vector<std::string>& lines) const;
    std::string error_box(const std::string& title, const std::vector<std::string>& lines) const;
    std::string info_box(const std::string& title, const std::vector<std::string>& lines) const;
    std::string badge(const std::string& text, Color color) const;

    static int terminal_width();

private:
    Theme();
    bool enabled_ = true;
};

// 便捷函数
const Theme& theme();

}  // namespace scheduler::ui
