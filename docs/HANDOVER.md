# 交接与维护文档（HANDOVER）

> 面向接手本项目的同事。读完本文你应该能：**在半小时内把环境跑起来**、
> **知道出问题去哪里查**、**知道要扩展功能该改哪个文件**。

---

## 1. 环境依赖与搭建

| 依赖 | 版本要求 | 检查命令 |
| --- | --- | --- |
| g++（C++17） | ≥ 9（本项目用 13.3 验证） | `g++ --version` |
| CMake | ≥ 3.16 | `cmake --version` |
| MySQL 客户端开发包 | 8.0（`mysql.h` + `libmysqlclient`） | 见下方说明 |
| MySQL 服务端 | 8.0 | `bash scripts/mysql_ctl.sh status` |

### MySQL 的两种准备方式

**方式 A：本机已有 MySQL（推荐生产/已有环境）**

```bash
sudo apt install mysql-server libmysqlclient-dev
# 建库建账号
mysql -uroot -p -e "CREATE DATABASE course_scheduler DEFAULT CHARACTER SET utf8mb4;
  CREATE USER 'scheduler'@'127.0.0.1' IDENTIFIED WITH mysql_native_password BY 'Scheduler@123';
  GRANT ALL ON course_scheduler.* TO 'scheduler'@'127.0.0.1'; FLUSH PRIVILEGES;"
```

**方式 B：无 root 权限 / 不想动系统环境（本仓库默认路径）**

```bash
bash scripts/setup_local_mysql.sh
```

脚本会把 MySQL 8.0 **安装到 `~/.local/mysql`**（解压 Ubuntu 二进制包，普通用户运行），并完成：
生成 `my.cnf` → `--initialize-insecure` 初始化数据目录 → 启动 `mysqld`（127.0.0.1:3306）→
创建库 `course_scheduler` 与账号 `scheduler`。相关路径：

```
~/.local/mysql/usr/sbin/mysqld      服务端二进制
~/.local/mysql/usr/include/mysql    C API 头文件(编译用)
~/.local/mysql/lib/libmysqlclient.so 链接用软链接 → 系统 libmysqlclient.so.21
~/.local/mysql/data                 数据目录
~/.local/mysql/log/error.log        错误日志(排错第一站)
~/.local/mysql/my.cnf               实例配置
```

> 编译时 CMake 会优先在该前缀下寻找头文件与库；若你的 MySQL 装在别处，
> 用 `cmake -DMYSQL_PREFIX=/your/prefix ..` 指定即可。

### 构建与运行

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/scheduler init-db      # 建表 + 导演示数据
./build/scheduler              # 终端界面
./build/scheduler_tests        # 单元测试
```

配置项统一放在项目根的 `.env`（模板 `.env.example`），环境变量优先级更高。

---

## 2. 代码阅读路线（建议顺序）

| 顺序 | 文件 | 为什么先读它 |
| --- | --- | --- |
| 1 | `include/scheduler/types.hpp` | 领域实体与枚举，和数据库表一一对应 |
| 2 | `sql/01_schema.sql` | 表结构、唯一索引、CHECK 约束（冲突的兜底防线在这里） |
| 3 | `src/graph/dag.cpp` | **核心算法**：Kahn 拓扑排序 + 环路定位（注释最详细） |
| 4 | `src/rules.cpp` | 纯函数业务规则：先修 / 时间冲突 / 学分 / 教室 / 时段排序 |
| 5 | `src/services/scheduling_service.cpp` | 排课主流程（`generate` 六步）+ 纯算法 `run_placement` |
| 6 | `src/services/enrollment_service.cpp` | 学生选课八步校验、退课、查课、先修进度 |
| 7 | `src/db/database.cpp` | MySQL C API 封装：预处理语句、结果集按字符串读取、事务 |
| 8 | `src/ui/widgets.cpp` | 终端可视化：表格、周课表网格（含中英文宽度对齐） |
| 9 | `tests/*.cpp` | 用测试理解边界与设计意图（算法测试不依赖数据库，跑得快） |

分层原则：**ui → services → rules/graph → repositories → db**，禁止反向依赖；
`rules` 与 `graph` 是纯逻辑，不引用 `db` 与 `repositories`。

---

## 3. 常见故障排查

| 症状 | 排查步骤 |
| --- | --- |
| 启动报 `无法连接 MySQL (...)[2002]` | ① `bash scripts/mysql_ctl.sh status`；② 看 `~/.local/mysql/log/error.log`；③ 检查 `.env` 的 `DB_HOST/DB_PORT/DB_USER/DB_PASSWORD` |
| 报 `Unknown database 'course_scheduler'` | 执行 `./build/scheduler init-db` |
| 报 `Access denied for user 'scheduler'` | 账号口令不一致：重跑 `scripts/setup_local_mysql.sh`（幂等）或改 `.env` |
| 中文表格错位/换行 | 终端宽度不足（< 80 列），拉宽窗口；`NO_COLOR=1` 可关闭颜色 |
| 排课失败班级过多 | 看报告里的失败原因表：教师工作量上限 / 资源不足；调整 `teachers.max_classes` 或增加教室 |
| 循环依赖报错 | `./build/scheduler cycle-check` 查看环路，再到界面菜单 2 删边 |
| 选课报「尚未完成排课」 | 先执行 `./build/scheduler schedule`（或把 `.env` 的 `REQUIRE_SCHEDULED_BEFORE_ENROLL=false`） |
| 基准测试数字波动大 | 属正常：演示规模小，耗时在几十微秒量级；用 `scaling`（每档重复 20 轮）看趋势更稳 |
| 编译报找不到 mysql.h | `cmake -DMYSQL_PREFIX=$HOME/.local/mysql ..` 或安装 `libmysqlclient-dev` |
| **VS Code 里 `#include "scheduler/xxx.hpp"` / `#include <mysql/mysql.h>` 报红** | 编辑器缺少编译命令。① 执行一次 `cmake -S . -B build` 生成 `build/compile_commands.json`；② `Ctrl+Shift+P` → `C/C++: Reset IntelliSense Database`（或 `Developer: Reload Window`）；③ 确认状态栏配置为 `Linux-GCC-C++17 (项目配置)`；④ 若 MySQL 在别处，改 `cmake -DMYSQL_PREFIX=...` 与 `.vscode/c_cpp_properties.json` 的 `includePath` |
| VS Code 报 `sys/ioctl.h` 找不到 | 该头文件在 `/usr/include/x86_64-linux-gnu/`（多架构目录），已在 `c_cpp_properties.json` 显式列入；若仍报错说明 IntelliSense 没用 g++ 的默认搜索路径，确认 `compilerPath` 为 `/usr/bin/g++` |
| CMake Tools 提示未配置 | 命令面板执行 `CMake: Configure`；构建目录已固定为 `${workspaceFolder}/build` |
| C/C++ 输出面板提示「缺少预期的文件 …cpptools-1.33.8/bin/cpptools」 | 说明磁盘上残留了**废弃版本**的扩展目录（VS Code 已在 `extensions/.obsolete` 中标记，但未删除）。清理方法：删除 `~/.vscode-server/extensions/ms-vscode.cpptools-<旧版本>*` 与 `~/.vscode/extensions/ms-vscode.cpptools-<旧版本>*`，只保留当前注册版本（`extensions.json` 中登记的 `-linux-x64` 目录），然后重载窗口 |
| IntelliSense 解析异常/占满磁盘 | `~/.cache/vscode-cpptools/ipch` 是预编译头缓存，长期使用可能增长到数 GB，可安全删除（会按需重建）：`rm -rf ~/.cache/vscode-cpptools/ipch` |
| 需要确认 IntelliSense 用了哪些 include 路径 | 把 `.vscode/settings.json` 的 `C_Cpp.loggingLevel` 改成 `Debug`，查看「输出 → C/C++」面板中的「文件夹 … 将编入索引」与 `didChangeCompileCommands` 记录 |

诊断命令速查：

```bash
./build/scheduler stats           # 学期统计 + 冲突自检
./build/scheduler cycle-check     # 环检测(exit 2 表示有环, 便于 CI 判定)
bash scripts/mysql_ctl.sh status  # 实例状态
bash scripts/mysql_ctl.sh cli     # 直接用 root 打开 mysql 客户端
tail -50 ~/.local/mysql/log/error.log
```

---

## 4. 数据与运维注意事项

- **重建演示数据**：`./build/scheduler init-db`（会 `DROP` 后重建所有表，仅在演示环境使用）。
- **配置 current 学期**：`semesters.is_current = 1` 与 `.env` 的 `CURRENT_SEMESTER` 必须一致，
  否则界面显示的学期与写到库里的学期会对不上。
- **排课是"覆盖式"写入**：`generate()` 在一个事务里「删除本学期旧排课 → 写入新结果 → 写审计」，
  不会出现半新半旧；但**没有历史版本回滚**，若需要请先 `mysqldump` 备份。
- **审计表 `scheduling_runs`** 记录了每次运行的算法、耗时、成功/失败数量与失败原因，
  排课异常时先看它（界面菜单 `9 排课审计记录`）。
- **口令**：演示环境为 `SHA2(学号#123456)`；生产必须替换为 bcrypt/argon2 每人随机盐，
  并把 `.env` 中的默认口令配置去掉。

---

## 5. 扩展点（按优先级）

| 需求 | 改动位置 | 提示 |
| --- | --- | --- |
| 增加"单双周"或"连排 2 大节"课程 | `sql/01_schema.sql` 的 `schedule_entries` 唯一索引 + `rules::order_candidate_slots` | 唯一索引要放宽到包含周次 |
| 软约束加权（更偏好上午 / 教师集中排课） | `rules::order_candidate_slots` / `rank_classrooms` | 把排序键换成可配置代价函数 |
| 真正的"最优解" | 新增 `services/optimizer`，以 `run_placement` 的结果为初始可行解做局部搜索 | `PlacementOutcome` 已给出统一的解表示 |
| 行政班统一排课（班级级冲突） | `rules::ConflictMatrix` 增加"行政班-时段"维度 | 需要在 `run_placement` 中读入班级信息 |
| Web API / 图形界面 | 复用 `services/*`，把 `ui/` 换成 HTTP 层 | `Database` 需替换为连接池版本 |
| 并发排课 | `Database` 连接池 + `scheduling_runs` 加批次锁 | 事务边界已在 `generate()` 中收敛 |
| 更多统计报表 | `services/query_service.cpp` + 对应视图 | 新增视图写进 `sql/01_schema.sql` |

---

## 6. 验收清单（交付前自查）

```bash
bash run.sh test            # 28 个单元测试全绿(含 500 张随机图的判环准确率对照)
bash run.sh init            # 数据可重建
./build/scheduler cycle-check   # 返回码 0
./build/scheduler schedule      # 成功率 100%, 三类冲突自检为 0
./build/scheduler timetable 2024001   # 周课表渲染正常(中文对齐)
./build/scheduler available 2026001   # 不可选原因标注正确
./build/scheduler benchmark 10        # 基准测试可复现
bash run.sh demo            # 完整业务流程一次性走通
```

对齐检查（每次改完代码都建议跑）：

- `cmake --build build -j` 无 warning（`-Wall -Wextra -Wpedantic` 全程打开）；
- 新增业务规则时**同步补 `tests/test_rules.cpp` 用例**；
- 新增算法分支时**同步补 `tests/test_scheduling.cpp` 用例**；
- 改数据库结构时**同步更新 `docs/DATABASE.md` 与 `sql/01_schema.sql` 的注释**。

---

## 7. 已知限制（交接时必须知道）

1. 排课产出的是**可行解**，不保证"最舒适"的软约束最优；
2. 单个课次 = 一个"周固定时段"，不支持单双周/多时段课程；
3. 排课未建模学生个人冲突（由选课环节的时间冲突校验兜住）；
4. 面向单线程 CLI，未做并发控制；
5. 演示数据规模较小（30 个教学班），效率结论请连同规模一起引用（见 `docs/DESIGN.md` 第 8 节）。
