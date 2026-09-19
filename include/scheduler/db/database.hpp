// ============================================================================
//  db/database.hpp —— MySQL C API 的 RAII 封装
//
//  设计要点:
//    1) RAII: 构造/连接失败抛 DatabaseError, 析构自动释放 MYSQL_STMT / MYSQL;
//    2) 参数化查询: 所有带外部输入的 SQL 都走 prepared statement(占位符 ?),
//       从根本上避免 SQL 注入(见 Statement::bind_strings);
//    3) 结果集统一读成"字符串单元格"(Row), 由上层按需转换;
//    4) 事务: transaction(fn) 正常返回即提交, 抛异常则回滚 —— 排课落库、
//       学生选课等多表写入都依赖这一语义保证一致性。
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <mysql/mysql.h>

#include "scheduler/config.hpp"
#include "scheduler/exceptions.hpp"

namespace scheduler::db {

// ---------------------------------------------------------------------------
//  结果集的一行: 列名 -> 值(字符串) + NULL 标记
// ---------------------------------------------------------------------------
class Row {
public:
    bool has(const std::string& column) const;
    bool is_null(const std::string& column) const;

    std::string get(const std::string& column, const std::string& fallback = "") const;
    int get_int(const std::string& column, int fallback = 0) const;
    long long get_long(const std::string& column, long long fallback = 0) const;
    double get_double(const std::string& column, double fallback = 0.0) const;
    bool get_bool(const std::string& column, bool fallback = false) const;
    std::optional<double> get_nullable_double(const std::string& column) const;
    std::optional<int> get_nullable_int(const std::string& column) const;

    void set(const std::string& column, const std::string& value);
    void set_null(const std::string& column);

    const std::map<std::string, std::string>& cells() const { return cells_; }

private:
    std::map<std::string, std::string> cells_;                  // 非 NULL 的值
    std::unordered_map<std::string, bool> present_;             // 列是否存在
};

class ResultSet {
public:
    std::size_t size() const { return rows_.size(); }
    bool empty() const { return rows_.empty(); }
    const Row& at(std::size_t index) const { return rows_[index]; }
    const Row& first() const { return rows_.front(); }
    const std::vector<Row>& rows() const { return rows_; }
    const std::vector<std::string>& columns() const { return columns_; }

    void add_column(const std::string& name) { columns_.push_back(name); }
    void add_row(Row row) { rows_.push_back(std::move(row)); }

private:
    std::vector<std::string> columns_;
    std::vector<Row> rows_;
};

// ---------------------------------------------------------------------------
//  预处理语句(参数化查询): 参数按位置绑定, 服务端解析 SQL, 不做字符串拼接
// ---------------------------------------------------------------------------
class Statement {
public:
    Statement(MYSQL* connection, const std::string& sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(const std::vector<std::string>& parameters);
    ResultSet fetch_all();          // 用于 SELECT
    std::uint64_t execute();        // 用于 INSERT/UPDATE/DELETE
    std::uint64_t affected_rows() const;

private:
    MYSQL* connection_ = nullptr;   // 仅借用, 不负责释放
    MYSQL_STMT* statement_ = nullptr;
    std::string sql_;
    std::vector<std::string> parameters_;         // 必须在执行期间保持稳定(地址被绑定)
    std::vector<unsigned long> parameter_lengths_; // 输入参数长度数组
    std::vector<MYSQL_BIND> parameter_binds_;
};

// ---------------------------------------------------------------------------
//  数据库连接
// ---------------------------------------------------------------------------
class Database {
public:
    explicit Database(const Config& config);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void connect();
    void disconnect();
    bool connected() const { return mysql_ != nullptr; }
    MYSQL* raw() { return mysql_; }

    ResultSet query(const std::string& sql);
    ResultSet query(const std::string& sql, const std::vector<std::string>& parameters);
    std::uint64_t execute(const std::string& sql);
    std::uint64_t execute(const std::string& sql, const std::vector<std::string>& parameters);

    // 事务包装: 块内任意异常都会回滚, 之后再抛出
    template <typename Fn>
    void transaction(Fn&& body) {
        ensure_connected();
        if (mysql_query(mysql_, "START TRANSACTION") != 0) {
            throw DatabaseError("开启事务失败: " + last_error());
        }
        try {
            body(*this);
            if (mysql_query(mysql_, "COMMIT") != 0) {
                throw DatabaseError("提交事务失败: " + last_error());
            }
        } catch (...) {
            mysql_query(mysql_, "ROLLBACK");
            throw;
        }
    }

    // ---- 运维/诊断 ----
    std::map<std::string, std::string> server_status();
    std::map<std::string, long long> table_counts(const std::vector<std::string>& tables);
    int run_sql_file(const std::string& path);          // 执行 .sql 脚本, 返回语句数
    static void create_database_if_missing(const Config& config);

private:
    const Config& config_;
    MYSQL* mysql_ = nullptr;

    void ensure_connected();
    std::string last_error() const;
};

// 把 .sql 脚本切分为语句(处理字符串字面量/注释里的分号), 供 run_sql_file 与测试使用
std::vector<std::string> split_sql_statements(const std::string& script);

}  // namespace scheduler::db
