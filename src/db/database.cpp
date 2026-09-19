// ============================================================================
//  db/database.cpp —— MySQL C API 封装实现
// ============================================================================
#include "scheduler/db/database.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

#include "scheduler/util/str.hpp"

namespace scheduler::db {
namespace {

constexpr std::size_t kCellBufferSize = 8192;   // 单个单元格读取缓冲(本项目字段都很短)

std::string describe_connection(const Config& config) {
    return config.db_user + "@" + config.db_host + ":" + std::to_string(config.db_port) +
           "/" + config.db_name;
}

}  // namespace

// ---------------------------------------------------------------------------
//  SQL 脚本切分: 用状态机跳过字符串字面量、反引号标识符与注释中的分号,
//  保证脚本里出现 "分隔符" 之类的内容时不会被切坏。
// ---------------------------------------------------------------------------
std::vector<std::string> split_sql_statements(const std::string& script) {
    std::vector<std::string> statements;
    std::string buffer;
    char quote = '\0';
    const std::size_t length = script.size();
    std::size_t i = 0;
    while (i < length) {
        const char ch = script[i];
        const char next = (i + 1 < length) ? script[i + 1] : '\0';
        if (quote == '\0' && ch == '-' && next == '-') {          // 行注释 --
            while (i < length && script[i] != '\n') ++i;
            continue;
        }
        if (quote == '\0' && ch == '#') {                          // 行注释 #
            while (i < length && script[i] != '\n') ++i;
            continue;
        }
        if (quote == '\0' && ch == '/' && next == '*') {            // 块注释 /* */
            i += 2;
            while (i + 1 < length && !(script[i] == '*' && script[i + 1] == '/')) ++i;
            i += 2;
            continue;
        }
        if (quote == '\0' && (ch == '\'' || ch == '"' || ch == '`')) {
            quote = ch;
            buffer.push_back(ch);
        } else if (quote != '\0' && ch == quote) {
            quote = '\0';
            buffer.push_back(ch);
        } else if (quote == '\0' && ch == ';') {
            const std::string statement = str::trim(buffer);
            if (!statement.empty()) statements.push_back(statement);
            buffer.clear();
        } else {
            buffer.push_back(ch);
        }
        ++i;
    }
    const std::string tail = str::trim(buffer);
    if (!tail.empty()) statements.push_back(tail);
    return statements;
}

// ---------------------------------------------------------------------------
//  Row
// ---------------------------------------------------------------------------
bool Row::has(const std::string& column) const {
    return present_.find(column) != present_.end() && present_.at(column);
}

bool Row::is_null(const std::string& column) const {
    return !has(column);
}

std::string Row::get(const std::string& column, const std::string& fallback) const {
    auto it = cells_.find(column);
    return it == cells_.end() ? fallback : it->second;
}

int Row::get_int(const std::string& column, int fallback) const {
    const std::string text = get(column);
    if (text.empty()) return fallback;
    try { return std::stoi(text); } catch (...) { return fallback; }
}

long long Row::get_long(const std::string& column, long long fallback) const {
    const std::string text = get(column);
    if (text.empty()) return fallback;
    try { return std::stoll(text); } catch (...) { return fallback; }
}

double Row::get_double(const std::string& column, double fallback) const {
    const std::string text = get(column);
    if (text.empty()) return fallback;
    try { return std::stod(text); } catch (...) { return fallback; }
}

bool Row::get_bool(const std::string& column, bool fallback) const {
    const std::string text = get(column);
    if (text.empty()) return fallback;
    return text == "1" || text == "true" || text == "TRUE";
}

std::optional<double> Row::get_nullable_double(const std::string& column) const {
    if (is_null(column)) return std::nullopt;
    const std::string text = get(column);
    if (text.empty()) return std::nullopt;
    try { return std::stod(text); } catch (...) { return std::nullopt; }
}

std::optional<int> Row::get_nullable_int(const std::string& column) const {
    if (is_null(column)) return std::nullopt;
    const std::string text = get(column);
    if (text.empty()) return std::nullopt;
    try { return std::stoi(text); } catch (...) { return std::nullopt; }
}

void Row::set(const std::string& column, const std::string& value) {
    cells_[column] = value;
    present_[column] = true;
}

void Row::set_null(const std::string& column) {
    cells_.erase(column);
    present_[column] = true;   // 列为"存在但值为 NULL"
}

// ---------------------------------------------------------------------------
//  Statement: 预处理 + 参数绑定 + 结果读取
// ---------------------------------------------------------------------------
Statement::Statement(MYSQL* connection, const std::string& sql)
    : connection_(connection), sql_(sql) {
    if (connection_ == nullptr) throw DatabaseError("Statement 需要有效的数据库连接");
    statement_ = mysql_stmt_init(connection_);
    if (statement_ == nullptr) throw DatabaseError("mysql_stmt_init 失败(内存不足)");
    if (mysql_stmt_prepare(statement_, sql_.c_str(),
                           static_cast<unsigned long>(sql_.size())) != 0) {
        const std::string error = mysql_stmt_error(statement_);
        mysql_stmt_close(statement_);
        statement_ = nullptr;
        throw DatabaseError("预处理 SQL 失败: " + error + "\nSQL: " + sql_);
    }
}

Statement::~Statement() {
    if (statement_ != nullptr) mysql_stmt_close(statement_);
}

// 参数全部按字符串绑定: MySQL 会按目标列类型自行转换, 既避免了手工拼接 SQL,
// 也避免了数值参数的格式化问题。parameters_ 作为成员保存, 保证绑定期间地址稳定。
void Statement::bind(const std::vector<std::string>& parameters) {
    if (statement_ == nullptr) throw DatabaseError("Statement 未初始化");
    if (parameters.empty()) return;
    parameters_ = parameters;
    parameter_lengths_.assign(parameters_.size(), 0);
    parameter_binds_.assign(parameters_.size(), MYSQL_BIND{});
    for (std::size_t i = 0; i < parameters_.size(); ++i) {
        parameter_lengths_[i] = static_cast<unsigned long>(parameters_[i].size());
        MYSQL_BIND& binding = parameter_binds_[i];
        std::memset(&binding, 0, sizeof(MYSQL_BIND));
        binding.buffer_type = MYSQL_TYPE_STRING;
        binding.buffer = parameters_[i].data();
        binding.buffer_length = parameter_lengths_[i];
        binding.length = &parameter_lengths_[i];
    }
    if (mysql_stmt_bind_param(statement_, parameter_binds_.data()) != 0) {
        throw DatabaseError(std::string("绑定参数失败: ") + mysql_stmt_error(statement_));
    }
}

ResultSet Statement::fetch_all() {
    if (mysql_stmt_execute(statement_) != 0) {
        throw DatabaseError(std::string("执行预处理语句失败: ") + mysql_stmt_error(statement_) +
                            "\nSQL: " + sql_);
    }
    // store_result: 一次性把结果拉到客户端, 便于自由遍历
    if (mysql_stmt_store_result(statement_) != 0) {
        throw DatabaseError(std::string("读取结果集失败: ") + mysql_stmt_error(statement_));
    }
    ResultSet result;
    MYSQL_RES* metadata = mysql_stmt_result_metadata(statement_);
    const unsigned int column_count = metadata == nullptr ? 0u : mysql_num_fields(metadata);
    if (column_count == 0) {
        if (metadata != nullptr) mysql_free_result(metadata);
        return result;
    }

    MYSQL_FIELD* fields = mysql_fetch_fields(metadata);
    // 所有列统一按字符串读取(数值/时间/DECIMAL 由客户端库转换), 上层再按需转型
    std::vector<std::vector<char>> buffers(column_count,
                                           std::vector<char>(kCellBufferSize, '\0'));
    std::vector<unsigned long> lengths(column_count, 0);
    std::vector<MYSQL_BIND> bindings(column_count);
    std::unique_ptr<bool[]> is_null(new bool[column_count]);
    std::unique_ptr<bool[]> is_error(new bool[column_count]);
    for (unsigned int i = 0; i < column_count; ++i) {
        result.add_column(fields[i].name);
        std::memset(&bindings[i], 0, sizeof(MYSQL_BIND));
        bindings[i].buffer_type = MYSQL_TYPE_STRING;
        bindings[i].buffer = buffers[i].data();
        bindings[i].buffer_length = static_cast<unsigned long>(kCellBufferSize);
        bindings[i].length = &lengths[i];
        bindings[i].is_null = &is_null[i];
        bindings[i].error = &is_error[i];
    }
    if (mysql_stmt_bind_result(statement_, bindings.data()) != 0) {
        mysql_free_result(metadata);
        throw DatabaseError(std::string("绑定结果列失败: ") + mysql_stmt_error(statement_));
    }

    while (true) {
        const int status = mysql_stmt_fetch(statement_);
        if (status == MYSQL_NO_DATA) break;
        if (status == 1) {
            const std::string error = mysql_stmt_error(statement_);
            mysql_free_result(metadata);
            throw DatabaseError("读取结果行失败: " + error);
        }
        Row row;
        for (unsigned int i = 0; i < column_count; ++i) {
            if (is_null[i]) {
                row.set_null(fields[i].name);
            } else {
                const unsigned long length = std::min<unsigned long>(
                        lengths[i], static_cast<unsigned long>(kCellBufferSize));
                row.set(fields[i].name, std::string(buffers[i].data(), length));
            }
        }
        result.add_row(std::move(row));
    }
    mysql_stmt_free_result(statement_);
    mysql_free_result(metadata);
    return result;
}

std::uint64_t Statement::execute() {
    if (mysql_stmt_execute(statement_) != 0) {
        throw DatabaseError(std::string("执行预处理语句失败: ") + mysql_stmt_error(statement_) +
                            "\nSQL: " + sql_);
    }
    return affected_rows();
}

std::uint64_t Statement::affected_rows() const {
    return static_cast<std::uint64_t>(mysql_stmt_affected_rows(statement_));
}

// ---------------------------------------------------------------------------
//  Database
// ---------------------------------------------------------------------------
Database::Database(const Config& config) : config_(config) {}

Database::~Database() { disconnect(); }

void Database::connect() {
    if (mysql_ != nullptr) return;
    mysql_ = mysql_init(nullptr);
    if (mysql_ == nullptr) throw DatabaseError("mysql_init 失败(内存不足)");

    mysql_options(mysql_, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    unsigned int timeout = 8;
    mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    if (mysql_real_connect(mysql_, config_.db_host.c_str(), config_.db_user.c_str(),
                           config_.db_password.c_str(), config_.db_name.c_str(),
                           config_.db_port, nullptr, 0) == nullptr) {
        const std::string error = mysql_error(mysql_);
        mysql_close(mysql_);
        mysql_ = nullptr;
        throw DatabaseError("无法连接 MySQL (" + describe_connection(config_) + "): " + error +
                            "\n提示: 请先执行 bash scripts/setup_local_mysql.sh 启动本地实例, "
                            "并检查 .env 中的 DB_* 配置");
    }
}

void Database::disconnect() {
    if (mysql_ != nullptr) {
        mysql_close(mysql_);
        mysql_ = nullptr;
    }
}

void Database::ensure_connected() {
    if (mysql_ == nullptr) {
        connect();
        return;
    }
    // ping 失败说明连接已被服务端断开(如实例重启), 重连一次
    if (mysql_ping(mysql_) != 0) {
        disconnect();
        connect();
    }
}

std::string Database::last_error() const {
    return mysql_ == nullptr ? "(未连接)"
                             : ("[" + std::to_string(mysql_errno(mysql_)) + "] " +
                                mysql_error(mysql_));
}

ResultSet Database::query(const std::string& sql) {
    ensure_connected();
    if (mysql_query(mysql_, sql.c_str()) != 0) {
        throw DatabaseError("查询失败: " + last_error() + "\nSQL: " + sql);
    }
    MYSQL_RES* raw = mysql_store_result(mysql_);
    ResultSet result;
    if (raw == nullptr) {
        if (mysql_field_count(mysql_) > 0) {
            throw DatabaseError("读取结果集失败: " + last_error());
        }
        return result;   // 语句本身不产生结果集
    }
    const unsigned int column_count = mysql_num_fields(raw);
    MYSQL_FIELD* fields = mysql_fetch_fields(raw);
    for (unsigned int i = 0; i < column_count; ++i) result.add_column(fields[i].name);

    MYSQL_ROW raw_row = nullptr;
    while ((raw_row = mysql_fetch_row(raw)) != nullptr) {
        unsigned long* lengths = mysql_fetch_lengths(raw);
        Row row;
        for (unsigned int i = 0; i < column_count; ++i) {
            if (raw_row[i] == nullptr) {
                row.set_null(fields[i].name);
            } else {
                row.set(fields[i].name, std::string(raw_row[i], lengths[i]));
            }
        }
        result.add_row(std::move(row));
    }
    mysql_free_result(raw);
    return result;
}

ResultSet Database::query(const std::string& sql, const std::vector<std::string>& parameters) {
    ensure_connected();
    Statement statement(mysql_, sql);
    statement.bind(parameters);
    return statement.fetch_all();
}

std::uint64_t Database::execute(const std::string& sql) {
    ensure_connected();
    if (mysql_query(mysql_, sql.c_str()) != 0) {
        throw DatabaseError("执行失败: " + last_error() + "\nSQL: " + sql);
    }
    return static_cast<std::uint64_t>(mysql_affected_rows(mysql_));
}

std::uint64_t Database::execute(const std::string& sql,
                                const std::vector<std::string>& parameters) {
    ensure_connected();
    Statement statement(mysql_, sql);
    statement.bind(parameters);
    return statement.execute();
}

std::map<std::string, std::string> Database::server_status() {
    const ResultSet result = query(
            "SELECT VERSION() AS version, @@port AS port, @@datadir AS datadir, "
            "DATABASE() AS db_name, NOW() AS server_time, USER() AS current_user, "
            "(SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE()) "
            "AS table_count");
    std::map<std::string, std::string> status;
    if (result.empty()) return status;
    const Row& row = result.first();
    status["client_version"] = mysql_get_client_info();
    for (const std::string& column : result.columns()) {
        status[column] = row.get(column);
    }
    return status;
}

std::map<std::string, long long> Database::table_counts(
        const std::vector<std::string>& tables) {
    std::map<std::string, long long> counts;
    for (const std::string& table : tables) {
        const ResultSet result = query("SELECT COUNT(*) AS total FROM " + table);
        counts[table] = result.empty() ? 0 : result.first().get_long("total");
    }
    return counts;
}

int Database::run_sql_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw DatabaseError("无法打开 SQL 脚本: " + path);
    }
    std::ostringstream content;
    content << file.rdbuf();
    const std::vector<std::string> statements = split_sql_statements(content.str());
    int executed = 0;
    ensure_connected();
    for (const std::string& statement : statements) {
        if (mysql_query(mysql_, statement.c_str()) != 0) {
            const std::string preview = statement.substr(0, 120);
            throw DatabaseError("执行脚本 " + path + " 失败: " + last_error() +
                                "\n语句片段: " + preview);
        }
        ++executed;
    }
    return executed;
}

// 建库: 先不指定 database 连接服务器, 再 CREATE DATABASE IF NOT EXISTS,
// 这样"全新环境"下无需人工登进 MySQL 手工建库。
void Database::create_database_if_missing(const Config& config) {
    MYSQL* connection = mysql_init(nullptr);
    if (connection == nullptr) throw DatabaseError("mysql_init 失败(内存不足)");
    mysql_options(connection, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    if (mysql_real_connect(connection, config.db_host.c_str(), config.db_user.c_str(),
                           config.db_password.c_str(), nullptr, config.db_port, nullptr, 0) ==
        nullptr) {
        const std::string error = mysql_error(connection);
        mysql_close(connection);
        throw DatabaseError("无法连接 MySQL 服务器(" + describe_connection(config) +
                            "): " + error);
    }
    const std::string sql = "CREATE DATABASE IF NOT EXISTS " + config.db_name +
                            " DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci";
    if (mysql_query(connection, sql.c_str()) != 0) {
        const std::string error = mysql_error(connection);
        mysql_close(connection);
        throw DatabaseError("创建数据库失败: " + error);
    }
    mysql_close(connection);
}

}  // namespace scheduler::db
