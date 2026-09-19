// ============================================================================
//  util/str.cpp —— 字符串与终端显示工具实现
// ============================================================================
#include "scheduler/util/str.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

namespace scheduler::str {
namespace {

// 判断一个 Unicode 码点是否属于"宽字符"(中日韩、全角符号、Emoji 等)
bool is_wide_codepoint(unsigned int cp) {
    return (cp >= 0x1100 && cp <= 0x115F) ||    // 韩文字母
           (cp >= 0x2E80 && cp <= 0x303E) ||    // 中日韩部首/标点
           (cp >= 0x3041 && cp <= 0x33FF) ||    // 假名、注音、韩文兼容
           (cp >= 0x3400 && cp <= 0x4DBF) ||    // 中日韩扩展 A
           (cp >= 0x4E00 && cp <= 0x9FFF) ||    // 中日韩统一表意文字
           (cp >= 0xA000 && cp <= 0xA4CF) ||    // 彝文
           (cp >= 0xAC00 && cp <= 0xD7A3) ||    // 韩文音节
           (cp >= 0xF900 && cp <= 0xFAFF) ||    // 兼容表意文字
           (cp >= 0xFE30 && cp <= 0xFE6F) ||    // 兼容形式
           (cp >= 0xFF00 && cp <= 0xFF60) ||    // 全角 ASCII
           (cp >= 0xFFE0 && cp <= 0xFFE6) ||    // 全角符号
           (cp >= 0x1F300 && cp <= 0x1FAFF) ||  // Emoji
           (cp >= 0x20000 && cp <= 0x3FFFD);    // 扩展 B 及以上
}

// 解码 UTF-8: 返回码点并把 index 推进到下一个字符; byte_len 为该字符字节数。
// 遇到非法字节序列时按单字节降级处理, 保证永远不越界、不死循环。
unsigned int next_codepoint(const std::string& text, std::size_t& index, int& byte_len) {
    const unsigned char c = static_cast<unsigned char>(text[index]);
    unsigned int cp = c;
    byte_len = 1;
    if (c >= 0xF0) {
        byte_len = 4;
        cp = c & 0x07u;
    } else if (c >= 0xE0) {
        byte_len = 3;
        cp = c & 0x0Fu;
    } else if (c >= 0xC0) {
        byte_len = 2;
        cp = c & 0x1Fu;
    } else {
        index += 1;
        return cp;
    }
    for (int k = 1; k < byte_len; ++k) {
        if (index + static_cast<std::size_t>(k) >= text.size()) {
            byte_len = k;
            break;
        }
        cp = (cp << 6) | (static_cast<unsigned char>(text[index + static_cast<std::size_t>(k)]) & 0x3Fu);
    }
    index += static_cast<std::size_t>(byte_len);
    return cp;
}

}  // namespace

std::string trim(const std::string& text) {
    auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && !not_space(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && !not_space(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::string upper(const std::string& text) {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    std::istringstream stream(text);
    while (std::getline(stream, current, delimiter)) {
        parts.push_back(current);
    }
    if (!text.empty() && text.back() == delimiter) parts.emplace_back();
    return parts;
}

std::string join(const std::vector<std::string>& parts, const std::string& separator) {
    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) result += separator;
        result += parts[i];
    }
    return result;
}

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    if (from.empty()) return text;
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string repeat(const std::string& unit, int times) {
    std::string result;
    for (int i = 0; i < times; ++i) result += unit;
    return result;
}

int display_width(const std::string& text) {
    int width = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        int byte_len = 1;
        const unsigned int cp = next_codepoint(text, index, byte_len);
        width += is_wide_codepoint(cp) ? 2 : 1;
    }
    return width;
}

std::string pad_display(const std::string& text, int width, bool align_right) {
    const int current = display_width(text);
    if (current >= width) return text;
    const std::string padding(static_cast<std::size_t>(width - current), ' ');
    return align_right ? padding + text : text + padding;
}

std::string truncate_display(const std::string& text, int width) {
    if (display_width(text) <= width) return text;
    const std::string ellipsis = "…";
    const int ellipsis_width = display_width(ellipsis);
    std::string result;
    int used = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        const std::size_t start = index;
        int byte_len = 1;
        const unsigned int cp = next_codepoint(text, index, byte_len);
        const int char_width = is_wide_codepoint(cp) ? 2 : 1;
        if (used + char_width + ellipsis_width > width) break;
        result.append(text, start, index - start);
        used += char_width;
    }
    return result + ellipsis;
}

std::vector<std::string> wrap_display(const std::string& text, int width,
                                      std::size_t max_lines) {
    std::vector<std::string> lines;
    if (width <= 0 || max_lines == 0) return lines;
    const std::string ellipsis = "…";
    const int ellipsis_width = display_width(ellipsis);

    std::string current;
    int used = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        const std::size_t start = index;
        int byte_len = 1;
        const unsigned int cp = next_codepoint(text, index, byte_len);
        const int char_width = is_wide_codepoint(cp) ? 2 : 1;
        if (used + char_width > width) {
            lines.push_back(current);           // 当前行已满, 另起一行
            current.clear();
            used = 0;
            if (lines.size() + 1 >= max_lines) break;   // 只剩最后一行, 走截断逻辑
        }
        current.append(text, start, index - start);
        used += char_width;
    }
    // 处理溢出: 若还有剩余内容且已达最大行数, 在末行补省略号
    if (index < text.size()) {
        if (lines.size() + 1 >= max_lines) {
            while (!current.empty() && used + ellipsis_width > width) {
                // 去掉最后一个字符(按 UTF-8 回退一个字符)
                std::size_t cut = current.size();
                while (cut > 0 && (static_cast<unsigned char>(current[cut - 1]) & 0xC0) == 0x80) --cut;
                if (cut > 0) --cut;
                const int removed_width = display_width(current.substr(cut));
                current.erase(cut);
                used -= removed_width;
            }
            current += ellipsis;
        }
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

std::string number(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

std::string percent(double ratio, int decimals) {
    return number(ratio * 100.0, decimals) + "%";
}

std::string thousands(long long value) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    std::string result;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count && count % 3 == 0) result.push_back(',');
        result.push_back(*it);
        ++count;
    }
    if (value < 0) result.push_back('-');
    std::reverse(result.begin(), result.end());
    return result;
}

std::string duration_ms(long long milliseconds) {
    if (milliseconds < 1000) return std::to_string(milliseconds) + " ms";
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f s", static_cast<double>(milliseconds) / 1000.0);
    return buffer;
}

// 微秒级耗时格式化: 用于基准测试中"核心算法"这类耗时很短的指标
std::string duration_us(long long microseconds) {
    if (microseconds < 1000) return std::to_string(microseconds) + " µs";
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f ms", static_cast<double>(microseconds) / 1000.0);
    return buffer;
}

std::string now_datetime() {
    const std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
    localtime_r(&now, &tm_buf);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buffer;
}

std::string today() {
    const std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
    localtime_r(&now, &tm_buf);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm_buf);
    return buffer;
}

}  // namespace scheduler::str
