// ============================================================================
//  config.hpp —— 运行期配置
//  约定: 所有可调参数集中在 .env / 环境变量, 业务代码中不得出现硬编码的
//        数据库地址、学分上限、口令等魔法值, 便于多环境部署与交接。
// ============================================================================
#pragma once

#include <string>
#include <vector>

namespace scheduler {

struct Config {
    // ---- 数据库连接 ----
    std::string db_host = "127.0.0.1";
    int db_port = 3306;
    std::string db_user = "scheduler";
    std::string db_password = "Scheduler@123";
    std::string db_name = "course_scheduler";

    // ---- 业务规则 ----
    std::string current_semester = "2026-2027-1";
    double max_credits_per_semester = 25.0;   // 每学期选课学分上限
    int min_score_to_pass = 60;               // 先修课通过分数线
    bool require_scheduled_before_enroll = true;  // 未排课的教学班不允许选课
    std::string default_student_password = "123456";
    int max_candidate_slots = 25;             // 每班最多尝试的候选时段数
    double manual_minutes_per_class = 3.0;    // 人工排课经验值(分钟/班), 仅用于基准估算

    // ---- 界面 ----
    bool color = true;

    // ---- 路径 ----
    std::string project_root;   // 项目根目录(用于定位 sql/ 脚本)
    std::string env_file;       // 实际读取到的 .env 路径(空表示未找到)

    // 加载配置: 先读 env_file, 再用真实环境变量覆盖(环境变量优先级更高)
    static Config load(const std::string& project_root);

    // 供"系统信息"页展示(口令打码)
    std::vector<std::pair<std::string, std::string>> describe() const;

    std::string sql_dir() const { return project_root + "/sql"; }
};

// 全局配置(进程内单例, 首次调用时从磁盘加载)
const Config& app_config();

}  // namespace scheduler
