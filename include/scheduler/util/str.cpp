// ============================================================================
//  util/str.hpp —— 字符串与终端显示工具
//  为什么需要这一层:
//      终端表格对齐必须知道"显示宽度", 而中文字符占 2 个字符宽度、UTF-8 下
//      占 3 字节, 直接用 std::string::size() 计算会导致表格错位。
//      这里的 display_width / pad_display / truncate_display 三个函数
//      让所有界面组件都能安全地处理中英文混排。
// ============================================================================
#pragma once

#include <string>
#include <vector>

namespace scheduler::str {

std::string trim(const std::string& text);
std::string upper(const std::string& text);
bool starts_with(const std::string& text, const std::string& prefix);
bool ends_with(const std::string& text, const std::string& suffix);
std::vector<std::string> split(const std::string& text, char delimiter);
std::string join(const std::vector<std::string>& parts, const std::string& separator);
std::string replace_all(std::string text, const std::string& from, const std::string& to);
std::string repeat(const std::string& unit, int times);

// ---- 终端显示宽度(中文/全角算 2) ----
int display_width(const std::string& text);
std::string pad_display(const std::string& text, int width, bool align_right = false);
std::string truncate_display(const std::string& text, int width);
// 按显示宽度折行: 最多 max_lines 行, 超出部分在末行以 … 结尾; 不切断 UTF-8 字符
std::vector<std::string> wrap_display(const std::string& text, int width,
                                      std::size_t max_lines = 2);

// ---- 文本与数值格式化 ----
std::string number(double value, int decimals = 1);
std::string percent(double ratio, int decimals = 1);
std::string thousands(long long value);
std::string duration_ms(long long milliseconds);
std::string duration_us(long long microseconds);
std::string now_datetime();
std::string today();

}  // namespace scheduler::str
