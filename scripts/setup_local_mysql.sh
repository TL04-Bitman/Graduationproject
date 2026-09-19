#!/usr/bin/env bash
# ============================================================================
#  scripts/setup_local_mysql.sh —— 无需 root 权限安装并初始化本地 MySQL 实例
#
#  适用场景: 开发机/演示机上没有 MySQL, 又没有 sudo 权限(或不想污染系统环境)。
#  做法: 从 Ubuntu 源下载 MySQL 二进制包, 解压到 $MYSQL_HOME(默认 ~/.local/mysql),
#        在用户目录下初始化独立数据目录并以普通用户启动, 端口 3306, 仅监听本机。
#
#  执行: bash scripts/setup_local_mysql.sh
#  幂等: 已安装则跳过下载/解压, 已初始化则跳过初始化, 可重复执行
# ============================================================================
set -euo pipefail

MYSQL_HOME="${MYSQL_HOME:-$HOME/.local/mysql}"
MYSQL_BASE="$MYSQL_HOME/usr"
DOWNLOAD_DIR="${MYSQL_DOWNLOAD_DIR:-$HOME/.local/mysql-dl}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
DB_NAME="${DB_NAME:-course_scheduler}"
DB_USER="${DB_USER:-scheduler}"
DB_PASSWORD="${DB_PASSWORD:-Scheduler@123}"
ROOT_PASSWORD="${ROOT_PASSWORD:-root123}"
SERVER_VERSION="${SERVER_VERSION:-8.0.36-2ubuntu3}"

log()  { printf '\033[36m[MySQL]\033[0m %s\n' "$*"; }
warn() { printf '\033[33m[警告]\033[0m %s\n' "$*"; }
die()  { printf '\033[31m[错误]\033[0m %s\n' "$*" >&2; exit 1; }

log "安装目录: $MYSQL_HOME"

# 本地前缀下的 MySQL 依赖库必须先加入搜索路径(mysqld/mysql 均依赖它)
export LD_LIBRARY_PATH="$MYSQL_BASE/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"

# ---------------------------------------------------------------------------
# 1. 下载并解压 MySQL 二进制包(仅首次)
# ---------------------------------------------------------------------------
if [ ! -x "$MYSQL_BASE/sbin/mysqld" ]; then
    mkdir -p "$DOWNLOAD_DIR"
    log "从软件源下载 MySQL 8.0 二进制包(约 25 MB)..."
    ( cd "$DOWNLOAD_DIR" && apt-get download \
        "mysql-server-core-8.0=$SERVER_VERSION" \
        "mysql-client-core-8.0=$SERVER_VERSION" \
        "libmysqlclient-dev=$SERVER_VERSION" \
        libaio1t64 libmecab2 libprotobuf-lite32t64 )

    log "解压到 $MYSQL_HOME ..."
    mkdir -p "$MYSQL_HOME"
    for deb in "$DOWNLOAD_DIR"/*.deb; do
        dpkg-deb -x "$deb" "$MYSQL_HOME"
    done
else
    log "检测到已安装的 mysqld: $("$MYSQL_BASE/sbin/mysqld" --version | head -1), 跳过下载"
fi

# 链接库: 系统已装 libmysqlclient21, 这里在本地前缀下建软链接, 便于 -lmysqlclient 链接
mkdir -p "$MYSQL_HOME/lib"
SYSTEM_CLIENT_LIB="$(ls /usr/lib/x86_64-linux-gnu/libmysqlclient.so.21.* 2>/dev/null | head -1 || true)"
if [ -n "$SYSTEM_CLIENT_LIB" ] && [ ! -e "$MYSQL_HOME/lib/libmysqlclient.so" ]; then
    ln -sfn "$SYSTEM_CLIENT_LIB" "$MYSQL_HOME/lib/libmysqlclient.so"
fi

# ---------------------------------------------------------------------------
# 2. 运行期目录与配置文件
# ---------------------------------------------------------------------------
mkdir -p "$MYSQL_HOME"/{data,run,log,files,tmp}
CONFIG_FILE="$MYSQL_HOME/my.cnf"
if [ ! -f "$CONFIG_FILE" ]; then
    log "生成配置文件 $CONFIG_FILE"
    cat > "$CONFIG_FILE" <<EOF
[mysqld]
basedir                 = $MYSQL_BASE
datadir                 = $MYSQL_HOME/data
plugin-dir              = $MYSQL_BASE/lib/mysql/plugin
lc-messages-dir         = $MYSQL_BASE/share/mysql
port                    = $MYSQL_PORT
bind-address            = 127.0.0.1
socket                  = $MYSQL_HOME/run/mysqld.sock
pid-file                = $MYSQL_HOME/run/mysqld.pid
log-error               = $MYSQL_HOME/log/error.log
secure-file-priv        = $MYSQL_HOME/files
character-set-server    = utf8mb4
collation-server        = utf8mb4_0900_ai_ci
default-storage-engine  = InnoDB
skip-name-resolve
mysqlx                  = 0
max_connections         = 200

[client]
port                    = $MYSQL_PORT
socket                  = $MYSQL_HOME/run/mysqld.sock
default-character-set   = utf8mb4
EOF
fi

export LD_LIBRARY_PATH="$MYSQL_BASE/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
MYSQLD="$MYSQL_BASE/sbin/mysqld"
MYSQL="$MYSQL_BASE/bin/mysql"

# ---------------------------------------------------------------------------
# 3. 初始化数据目录(仅首次): --initialize-insecure 会创建空口令的 root@localhost
# ---------------------------------------------------------------------------
if [ ! -d "$MYSQL_HOME/data/mysql" ]; then
    log "初始化数据目录(约 10 秒)..."
    "$MYSQLD" --defaults-file="$CONFIG_FILE" --initialize-insecure \
              --log-error="$MYSQL_HOME/log/init.log"
    log "初始化完成: $MYSQL_HOME/data"
else
    log "数据目录已存在, 跳过初始化"
fi

# ---------------------------------------------------------------------------
# 4. 启动实例
#    root 可能已设置口令, 因此先试"带口令", 再退化为"无口令"(首次初始化时)
# ---------------------------------------------------------------------------
mysql_root_ok() {
    "$MYSQL" --socket="$MYSQL_HOME/run/mysqld.sock" -uroot -p"$ROOT_PASSWORD" \
        -e 'SELECT 1' >/dev/null 2>&1 ||
    "$MYSQL" --socket="$MYSQL_HOME/run/mysqld.sock" -uroot -e 'SELECT 1' >/dev/null 2>&1
}

if mysql_root_ok; then
    log "MySQL 已在运行"
else
    log "启动 mysqld ..."
    nohup "$MYSQLD" --defaults-file="$CONFIG_FILE" >>"$MYSQL_HOME/log/stdout.log" 2>&1 &
    for _ in $(seq 1 30); do
        if mysql_root_ok; then
            break
        fi
        sleep 1
    done
fi

# ---------------------------------------------------------------------------
# 5. 设置 root 口令, 创建业务库与账号
#    业务账号使用 mysql_native_password, 便于各类 MySQL 客户端直连;
#    生产环境建议改用 caching_sha2_password 并开启 TLS。
# ---------------------------------------------------------------------------
configure_accounts() {
    "$MYSQL" "$@" <<SQL
ALTER USER 'root'@'localhost' IDENTIFIED WITH mysql_native_password BY '$ROOT_PASSWORD';
CREATE DATABASE IF NOT EXISTS \`$DB_NAME\` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
CREATE USER IF NOT EXISTS '$DB_USER'@'localhost' IDENTIFIED WITH mysql_native_password BY '$DB_PASSWORD';
CREATE USER IF NOT EXISTS '$DB_USER'@'127.0.0.1' IDENTIFIED WITH mysql_native_password BY '$DB_PASSWORD';
GRANT ALL PRIVILEGES ON \`$DB_NAME\`.* TO '$DB_USER'@'localhost';
GRANT ALL PRIVILEGES ON \`$DB_NAME\`.* TO '$DB_USER'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL
}

log "配置 root 口令、业务库与账号..."
# 首次初始化时 root 无口令; 已初始化过则用配置中的口令登录, 两条路都试一遍
configure_accounts --socket="$MYSQL_HOME/run/mysqld.sock" -uroot -p"$ROOT_PASSWORD" 2>/dev/null ||
    configure_accounts --socket="$MYSQL_HOME/run/mysqld.sock" -uroot

"$MYSQL" -h127.0.0.1 -P"$MYSQL_PORT" -u"$DB_USER" -p"$DB_PASSWORD" \
    -e "SELECT VERSION() AS mysql版本;"
log "完成。数据库: $DB_NAME  账号: $DB_USER  口令: $DB_PASSWORD"
log "下一步: bash run.sh init   然后   bash run.sh"
