#!/usr/bin/env bash
# ============================================================================
#  scripts/mysql_ctl.sh —— 本地 MySQL 实例的启停与状态查看
#  用法: bash scripts/mysql_ctl.sh {start|stop|status|cli}
# ============================================================================
set -euo pipefail

MYSQL_HOME="${MYSQL_HOME:-$HOME/.local/mysql}"
MYSQL_BASE="$MYSQL_HOME/usr"
CONFIG_FILE="$MYSQL_HOME/my.cnf"
SOCKET="$MYSQL_HOME/run/mysqld.sock"
export LD_LIBRARY_PATH="$MYSQL_BASE/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"

MYSQLD="$MYSQL_BASE/sbin/mysqld"
MYSQL="$MYSQL_BASE/bin/mysql"
ROOT_PASSWORD="${ROOT_PASSWORD:-root123}"

usage() { echo "用法: $0 {start|stop|status|cli}"; exit 1; }

# root 口令可能已设置, 也可能尚未设置(刚初始化), 两种情况都兼容
root_ok() {
    "$MYSQL" --socket="$SOCKET" -uroot -p"$ROOT_PASSWORD" -e 'SELECT 1' >/dev/null 2>&1 ||
    "$MYSQL" --socket="$SOCKET" -uroot -e 'SELECT 1' >/dev/null 2>&1
}

mysql_run() {
    if "$MYSQL" --socket="$SOCKET" -uroot -p"$ROOT_PASSWORD" -e 'SELECT 1' >/dev/null 2>&1; then
        "$MYSQL" --socket="$SOCKET" -uroot -p"$ROOT_PASSWORD" "$@"
    else
        "$MYSQL" --socket="$SOCKET" -uroot "$@"
    fi
}

start() {
    if root_ok; then
        echo "MySQL 已在运行 (socket: $SOCKET)"
        return
    fi
    nohup "$MYSQLD" --defaults-file="$CONFIG_FILE" >>"$MYSQL_HOME/log/stdout.log" 2>&1 &
    for _ in $(seq 1 30); do
        if root_ok; then
            echo "MySQL 启动成功 (端口 3306)"
            return
        fi
        sleep 1
    done
    echo "启动超时, 请查看 $MYSQL_HOME/log/error.log" >&2
    exit 1
}

stop() {
    if [ -f "$MYSQL_HOME/run/mysqld.pid" ]; then
        local pid
        pid="$(cat "$MYSQL_HOME/run/mysqld.pid")"
        kill "$pid" && echo "已停止 MySQL (pid $pid)"
    else
        pkill -f "$MYSQLD --defaults-file=$CONFIG_FILE" 2>/dev/null && echo "已停止 MySQL" || echo "未发现运行中的实例"
    fi
}

status() {
    if root_ok; then
        mysql_run -e "SELECT VERSION() AS 版本, @@port AS 端口, @@datadir AS 数据目录;" 2>/dev/null
    else
        echo "MySQL 未运行"
    fi
}

cli() {
    shift || true
    mysql_run "${DB_NAME:-course_scheduler}" "$@"
}

case "${1:-}" in
    start)  start ;;
    stop)   stop ;;
    status) status ;;
    cli)    cli "$@" ;;
    *)      usage ;;
esac
