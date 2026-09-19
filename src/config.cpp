// ============================================================================
//  config.cpp —— 配置加载(文件 + 环境变量)
//  优先级: 环境变量 > .env 文件 > 代码内默认值
// ============================================================================
#include "scheduler/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

#include "scheduler/util/str.hpp"

namespace scheduler {
namespace {

std::map<std::string, std::string> read_env_file(const std::string& path) {
    std::map<std::string, std::string> values;
    std::ifstream file(path);
    if (!file.is_open()) return values;
    std::string line;
    while (std::getline(file, line)) {
        line = str::trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = str::trim(line.substr(0, pos));
        std::string value = str::trim(line.substr(pos + 1));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'')) {
            value = value.substr(1, value.size() - 2);
        }
        values[key] = value;
    }
    return values;
}

std::string lookup(const std::map<std::string, std::string>& file_values,
                   const std::string& key, const std::string& fallback) {
    if (const char* env = std::getenv(key.c_str()); env != nullptr && *env != '\0') {
        return env;   // 环境变量优先级最高
    }
    auto it = file_values.find(key);
    return it == file_values.end() ? fallback : it->second;
}

int to_int(const std::string& value, int fallback) {
    try { return std::stoi(value); } catch (...) { return fallback; }
}

double to_double(const std::string& value, double fallback) {
    try { return std::stod(value); } catch (...) { return fallback; }
}

bool to_bool(const std::string& value, bool fallback) {
    std::string lowered = str::upper(str::trim(value));
    if (lowered == "1" || lowered == "TRUE" || lowered == "YES" || lowered == "ON") return true;
    if (lowered == "0" || lowered == "FALSE" || lowered == "NO" || lowered == "OFF") return false;
    return fallback;
}

// 向上查找项目根目录(以 sql/01_schema.sql 或 CMakeLists.txt 为标志),
// 使程序无论从哪个目录启动都能定位 SQL 脚本。
std::string detect_project_root() {
    if (const char* env = std::getenv("PROJECT_ROOT"); env != nullptr && *env != '\0') {
        return env;
    }
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::current_path(ec);
    if (ec) return ".";
    for (int depth = 0; depth < 6 && !dir.empty(); ++depth) {
        if (std::filesystem::exists(dir / "sql" / "01_schema.sql", ec) ||
            std::filesystem::exists(dir / "CMakeLists.txt", ec)) {
            return dir.string();
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return std::filesystem::current_path(ec).string();
}

}  // namespace

Config Config::load(const std::string& root) {
    Config config;
    config.project_root = root.empty() ? detect_project_root() : root;
    const std::string env_path = config.project_root + "/.env";
    const std::map<std::string, std::string> file_values = read_env_file(env_path);
    config.env_file = std::filesystem::exists(env_path) ? env_path : "";

    config.db_host = lookup(file_values, "DB_HOST", config.db_host);
    config.db_port = to_int(lookup(file_values, "DB_PORT", std::to_string(config.db_port)), config.db_port);
    config.db_user = lookup(file_values, "DB_USER", config.db_user);
    config.db_password = lookup(file_values, "DB_PASSWORD", config.db_password);
    config.db_name = lookup(file_values, "DB_NAME", config.db_name);

    config.current_semester = lookup(file_values, "CURRENT_SEMESTER", config.current_semester);
    config.max_credits_per_semester = to_double(
            lookup(file_values, "MAX_CREDITS_PER_SEMESTER",
                   std::to_string(config.max_credits_per_semester)),
            config.max_credits_per_semester);
    config.min_score_to_pass = to_int(
            lookup(file_values, "MIN_SCORE_TO_PASS", std::to_string(config.min_score_to_pass)),
            config.min_score_to_pass);
    config.require_scheduled_before_enroll = to_bool(
            lookup(file_values, "REQUIRE_SCHEDULED_BEFORE_ENROLL",
                   config.require_scheduled_before_enroll ? "true" : "false"),
            config.require_scheduled_before_enroll);
    config.default_student_password =
            lookup(file_values, "DEFAULT_STUDENT_PASSWORD", config.default_student_password);
    config.max_candidate_slots = to_int(
            lookup(file_values, "MAX_CANDIDATE_SLOTS", std::to_string(config.max_candidate_slots)),
            config.max_candidate_slots);
    config.manual_minutes_per_class = to_double(
            lookup(file_values, "MANUAL_MINUTES_PER_CLASS",
                   std::to_string(config.manual_minutes_per_class)),
            config.manual_minutes_per_class);
    config.color = to_bool(lookup(file_values, "UI_COLOR", "true"), true);
    if (std::getenv("NO_COLOR") != nullptr) config.color = false;
    return config;
}

std::vector<std::pair<std::string, std::string>> Config::describe() const {
    return {
            {"数据库地址", db_host + ":" + std::to_string(db_port)},
            {"数据库名", db_name},
            {"数据库账号", db_user},
            {"数据库口令", "(已隐藏)"},
            {"当前学期", current_semester},
            {"学期学分上限", str::number(max_credits_per_semester, 1) + " 学分"},
            {"先修通过分数线", std::to_string(min_score_to_pass) + " 分"},
            {"选课前是否要求已排课", require_scheduled_before_enroll ? "是" : "否"},
            {"配置文件", env_file.empty() ? "(未找到 .env, 使用默认值)" : env_file},
            {"项目根目录", project_root},
    };
}

const Config& app_config() {
    static const Config config = Config::load("");
    return config;
}

}  // namespace scheduler
