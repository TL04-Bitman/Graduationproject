-- ============================================================================
--  学生课表排课系统 —— 数据库结构定义 (MySQL 8.0)
--  文件: sql/01_schema.sql
--  说明:
--    1) 本脚本可重复执行(幂等): 先 DROP 再 CREATE, 用于演示环境快速重建。
--    2) 课程先修关系存放在 course_prerequisites 中, 逻辑上构成一张有向图:
--         边方向: prereq_course_id -> course_id  (先修课 -> 后续课)
--       环检测由应用层(graph.py 的 Kahn 算法)负责, 数据库层用 CHECK 拦住自环。
--    3) schedule_entries 中冗余保存 teacher_id / course_id, 目的是让
--       "同一教师同一时段"、"同一教室同一时段" 两条业务约束可以用唯一索引兜底,
--       数据库层面 100% 防止双重排课(详见 docs/DATABASE.md)。
-- ============================================================================
SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

DROP VIEW  IF EXISTS v_student_timetable;
DROP VIEW  IF EXISTS v_class_timetable;
DROP VIEW  IF EXISTS v_prereq_edges;
DROP VIEW  IF EXISTS v_semester_statistics;
DROP TABLE IF EXISTS scheduling_runs;
DROP TABLE IF EXISTS enrollments;
DROP TABLE IF EXISTS schedule_entries;
DROP TABLE IF EXISTS teaching_classes;
DROP TABLE IF EXISTS students;
DROP TABLE IF EXISTS time_slots;
DROP TABLE IF EXISTS classrooms;
DROP TABLE IF EXISTS teachers;
DROP TABLE IF EXISTS course_prerequisites;
DROP TABLE IF EXISTS courses;
DROP TABLE IF EXISTS semesters;

-- ---------------------------------------------------------------------------
-- 1. 学期表
-- ---------------------------------------------------------------------------
CREATE TABLE semesters (
    id            INT UNSIGNED     NOT NULL AUTO_INCREMENT COMMENT '主键',
    semester_code VARCHAR(20)      NOT NULL                COMMENT '学期编码, 如 2025-2026-1',
    semester_name VARCHAR(50)      NOT NULL                COMMENT '学期名称',
    start_date    DATE             NOT NULL                COMMENT '开学日期(第 1 周周一)',
    total_weeks   TINYINT UNSIGNED NOT NULL DEFAULT 16     COMMENT '教学周数',
    is_current    TINYINT(1)       NOT NULL DEFAULT 0      COMMENT '是否当前学期',
    created_at    TIMESTAMP        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_semester_code (semester_code)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT '学期';

-- ---------------------------------------------------------------------------
-- 2. 课程表 (DAG 的顶点)
-- ---------------------------------------------------------------------------
CREATE TABLE courses (
    id                   INT UNSIGNED      NOT NULL AUTO_INCREMENT COMMENT '主键',
    course_code          VARCHAR(20)       NOT NULL                COMMENT '课程编号',
    course_name          VARCHAR(100)      NOT NULL                COMMENT '课程名称',
    dept                 VARCHAR(50)       NOT NULL                COMMENT '开课院系',
    credits              DECIMAL(3, 1)     NOT NULL                COMMENT '学分',
    total_hours          SMALLINT UNSIGNED NOT NULL                COMMENT '总学时',
    course_type          ENUM('required','elective','general') NOT NULL DEFAULT 'required'
                                                                   COMMENT '必修/选修/通识',
    class_hours_per_week TINYINT UNSIGNED  NOT NULL DEFAULT 2      COMMENT '每周排课节次数(决定生成几条排课记录)',
    status               ENUM('active','inactive') NOT NULL DEFAULT 'active' COMMENT '课程状态',
    created_at           TIMESTAMP         NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_course_code (course_code),
    KEY idx_courses_dept (dept),
    KEY idx_courses_type (course_type)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT '课程';

-- ---------------------------------------------------------------------------
-- 3. 课程先修关系表 (DAG 的边)
-- ---------------------------------------------------------------------------
CREATE TABLE course_prerequisites (
    id               INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    course_id        INT UNSIGNED NOT NULL COMMENT '后续课程(边的终点)',
    prereq_course_id INT UNSIGNED NOT NULL COMMENT '先修课程(边的起点)',
    created_at       TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_course_prereq (course_id, prereq_course_id),
    KEY idx_prereq_course (prereq_course_id),
    CONSTRAINT fk_prereq_course FOREIGN KEY (course_id)        REFERENCES courses (id) ON DELETE CASCADE,
    CONSTRAINT fk_prereq_pre    FOREIGN KEY (prereq_course_id) REFERENCES courses (id) ON DELETE CASCADE,
    -- 第一道防线: 自环(A 以自己为先修)必然构成环, 在写入阶段直接拒绝
    CONSTRAINT chk_prereq_no_selfloop CHECK (course_id <> prereq_course_id)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT '课程先修关系(DAG 边)';

-- ---------------------------------------------------------------------------
-- 4. 教师 / 教室 / 节次 (排课资源)
-- ---------------------------------------------------------------------------
CREATE TABLE teachers (
    id           INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    teacher_no   VARCHAR(20)  NOT NULL COMMENT '工号',
    teacher_name VARCHAR(50)  NOT NULL COMMENT '姓名',
    title        ENUM('教授','副教授','讲师','助教') NOT NULL DEFAULT '讲师' COMMENT '职称',
    dept         VARCHAR(50)  NOT NULL COMMENT '所属院系',
    max_classes  TINYINT UNSIGNED NOT NULL DEFAULT 4 COMMENT '单学期最多承担教学班数',
    PRIMARY KEY (id),
    UNIQUE KEY uk_teacher_no (teacher_no)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT '教师';

CREATE TABLE classrooms (
    id        INT UNSIGNED      NOT NULL AUTO_INCREMENT COMMENT '主键',
    room_code VARCHAR(20)       NOT NULL COMMENT '教室编号',
    building  VARCHAR(30)       NOT NULL COMMENT '教学楼',
    capacity  SMALLINT UNSIGNED NOT NULL COMMENT '容量(座位数)',
    room_type ENUM('general','computer','lab') NOT NULL DEFAULT 'general' COMMENT '普通教室/机房/实验室',
    PRIMARY KEY (id),
    UNIQUE KEY uk_room_code (room_code),
    KEY idx_room_capacity (capacity)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT '教室';

CREATE TABLE time_slots (
    id          INT UNSIGNED     NOT NULL AUTO_INCREMENT COMMENT '主键',
    day_of_week TINYINT UNSIGNED NOT NULL COMMENT '星期 1-7',
    period_no   TINYINT UNSIGNED NOT NULL COMMENT '第几大节 1-5',
    start_time  TIME             NOT NULL COMMENT '开始时间',
    end_time    TIME             NOT NULL COMMENT '结束时间',
    slot_weight TINYINT UNSIGNED NOT NULL DEFAULT 50 COMMENT '时段偏好权重(越小越优先占用)',
    PRIMARY KEY (id),
    UNIQUE KEY uk_day_period (day_of_week, period_no)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT '节次定义';

-- ---------------------------------------------------------------------------
-- 5. 教学班 (某学期某课程的一次开课, 排课与选课的最小单位)
-- ---------------------------------------------------------------------------
CREATE TABLE teaching_classes (
    id               INT UNSIGNED      NOT NULL AUTO_INCREMENT COMMENT '主键',
    class_code       VARCHAR(30)       NOT NULL COMMENT '教学班编号, 如 CS201-01',
    course_id        INT UNSIGNED      NOT NULL COMMENT '课程 ID',
    teacher_id       INT UNSIGNED      NOT NULL COMMENT '主讲教师',
    semester_code    VARCHAR(20)       NOT NULL COMMENT '所属学期',
    class_name       VARCHAR(120)      NOT NULL COMMENT '教学班名称',
    capacity         SMALLINT UNSIGNED NOT NULL COMMENT '班级容量',
    enrolled_count   SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '已选人数',
    expected_students SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '预期选课人数(排课时用于挑教室)',
    open_for_enroll  TINYINT(1)        NOT NULL DEFAULT 1 COMMENT '是否开放选课',
    created_at       TIMESTAMP         NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_class_code (class_code),
    KEY idx_tc_semester (semester_code),
    KEY idx_tc_course (course_id),
    KEY idx_tc_teacher (teacher_id),
    CONSTRAINT fk_tc_course  FOREIGN KEY (course_id)  REFERENCES courses (id),
    CONSTRAINT fk_tc_teacher FOREIGN KEY (teacher_id) REFERENCES teachers (id)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT '教学班';

-- ---------------------------------------------------------------------------
-- 6. 排课结果 (教务排课流程的产物: 一个教学班的一条"周固定时段"记录)
-- ---------------------------------------------------------------------------
CREATE TABLE schedule_entries (
    id                INT UNSIGNED     NOT NULL AUTO_INCREMENT COMMENT '主键',
    teaching_class_id INT UNSIGNED     NOT NULL COMMENT '教学班 ID',
    course_id         INT UNSIGNED     NOT NULL COMMENT '课程 ID(冗余, 便于查询)',
    teacher_id        INT UNSIGNED     NOT NULL COMMENT '教师 ID(冗余, 用于教师时段唯一约束)',
    classroom_id      INT UNSIGNED     NOT NULL COMMENT '教室 ID',
    semester_code     VARCHAR(20)      NOT NULL COMMENT '学期',
    day_of_week       TINYINT UNSIGNED NOT NULL COMMENT '星期',
    period_no         TINYINT UNSIGNED NOT NULL COMMENT '第几大节',
    week_start        TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '起始周',
    week_end          TINYINT UNSIGNED NOT NULL DEFAULT 16 COMMENT '结束周',
    created_at        TIMESTAMP        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    -- 约束1: 同一教学班同一时段只能排一次(一周同一时间上两次没有意义)
    UNIQUE KEY uk_class_slot (teaching_class_id, semester_code, day_of_week, period_no),
    -- 约束2: 同一教师同一时段不能出现在两个教室(教师冲突, 数据库兜底)
    UNIQUE KEY uk_teacher_slot (teacher_id, semester_code, day_of_week, period_no),
    -- 约束3: 同一教室同一时段不能安排两个教学班(教室冲突, 数据库兜底)
    UNIQUE KEY uk_room_slot (classroom_id, semester_code, day_of_week, period_no),
    KEY idx_se_semester (semester_code),
    CONSTRAINT fk_se_class FOREIGN KEY (teaching_class_id) REFERENCES teaching_classes (id) ON DELETE CASCADE,
    CONSTRAINT fk_se_course FOREIGN KEY (course_id)  REFERENCES courses (id),
    CONSTRAINT fk_se_teacher FOREIGN KEY (teacher_id) REFERENCES teachers (id),
    CONSTRAINT fk_se_room FOREIGN KEY (classroom_id) REFERENCES classrooms (id)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT '排课结果(周固定时段)';

-- ---------------------------------------------------------------------------
-- 7. 学生表 (学生端登录账号: 学号 + 口令)
-- ---------------------------------------------------------------------------
CREATE TABLE students (
    id            INT UNSIGNED       NOT NULL AUTO_INCREMENT COMMENT '主键',
    student_no    VARCHAR(20)        NOT NULL COMMENT '学号(登录账号)',
    student_name  VARCHAR(50)        NOT NULL COMMENT '姓名',
    gender        ENUM('M','F')      NOT NULL DEFAULT 'M' COMMENT '性别',
    grade_year    SMALLINT UNSIGNED  NOT NULL COMMENT '入学年份',
    major         VARCHAR(60)        NOT NULL COMMENT '专业',
    admin_class   VARCHAR(30)        NOT NULL COMMENT '行政班',
    dept          VARCHAR(50)        NOT NULL COMMENT '院系',
    status        ENUM('active','suspended','graduated') NOT NULL DEFAULT 'active' COMMENT '学籍状态',
    -- 口令存储: SHA2(CONCAT(学号,'#',明文口令), 256), 即"学号作为唯一盐值"。
    -- 演示环境用 sha256 便于 SQL 侧直接生成种子数据; 生产环境应改用 bcrypt/argon2。
    password_hash CHAR(64)           NOT NULL COMMENT '口令摘要',
    created_at    TIMESTAMP          NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_student_no (student_no),
    KEY idx_students_major (major),
    KEY idx_students_grade (grade_year)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT '学生';

-- ---------------------------------------------------------------------------
-- 8. 选课记录 (学生-教学班; 同时承担"历史成绩"职责, 是学生端排课的结果)
-- ---------------------------------------------------------------------------
CREATE TABLE enrollments (
    id                INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    student_id        INT UNSIGNED NOT NULL COMMENT '学生 ID',
    teaching_class_id INT UNSIGNED NOT NULL COMMENT '教学班 ID',
    semester_code     VARCHAR(20)  NOT NULL COMMENT '学期',
    status            ENUM('enrolled','completed','failed','dropped') NOT NULL DEFAULT 'enrolled'
                                   COMMENT '已选/已完成/不及格/已退课',
    score             DECIMAL(5, 2) NULL COMMENT '成绩(百分制)',
    selected_at       DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '选课时间',
    updated_at        TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_student_class (student_id, teaching_class_id, semester_code),
    KEY idx_enroll_student (student_id, semester_code),
    KEY idx_enroll_class (teaching_class_id),
    KEY idx_enroll_status (status),
    CONSTRAINT fk_enroll_student FOREIGN KEY (student_id) REFERENCES students (id) ON DELETE CASCADE,
    CONSTRAINT fk_enroll_class   FOREIGN KEY (teaching_class_id) REFERENCES teaching_classes (id) ON DELETE CASCADE
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT '选课/成绩记录';

-- ---------------------------------------------------------------------------
-- 9. 排课批次审计表 (每次一键排课都留痕, 便于交接与回溯)
-- ---------------------------------------------------------------------------
CREATE TABLE scheduling_runs (
    id               INT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    semester_code    VARCHAR(20)  NOT NULL COMMENT '学期',
    algorithm        VARCHAR(40)  NOT NULL COMMENT '算法标识, 如 kahn-topological-greedy',
    started_at       DATETIME     NOT NULL,
    finished_at      DATETIME     NOT NULL,
    duration_ms      INT UNSIGNED NOT NULL COMMENT '耗时(毫秒)',
    total_courses    INT UNSIGNED NOT NULL COMMENT '参与排课的课程数(顶点数)',
    total_classes    INT UNSIGNED NOT NULL COMMENT '教学班数',
    scheduled_entries INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '成功落库的排课记录数',
    failed_classes   INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '未能排出的教学班数',
    status           ENUM('success','partial','failed','cycle_detected') NOT NULL DEFAULT 'success',
    message          TEXT NULL COMMENT '附加信息/失败原因(如环路路径)',
    PRIMARY KEY (id),
    KEY idx_run_semester (semester_code)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT '排课批次审计';

SET FOREIGN_KEY_CHECKS = 1;

-- ---------------------------------------------------------------------------
-- 视图 1: 课程先修关系(边列表) —— 应用层 graph.py 直接读这张视图建图
-- ---------------------------------------------------------------------------
CREATE OR REPLACE VIEW v_prereq_edges AS
SELECT p.id                AS edge_id,
       p.course_id         AS course_id,
       c.course_code       AS course_code,
       c.course_name       AS course_name,
       c.credits           AS course_credits,
       p.prereq_course_id  AS prereq_course_id,
       pc.course_code      AS prereq_course_code,
       pc.course_name      AS prereq_course_name,
       pc.credits          AS prereq_credits
FROM course_prerequisites p
         JOIN courses c  ON c.id = p.course_id
         JOIN courses pc ON pc.id = p.prereq_course_id;

-- ---------------------------------------------------------------------------
-- 视图 2: 班级课表(教务视角: 教学班 -> 时段 -> 教室)
-- ---------------------------------------------------------------------------
CREATE OR REPLACE VIEW v_class_timetable AS
SELECT se.semester_code,
       tc.class_code,
       c.course_code,
       c.course_name,
       c.credits,
       t.teacher_name,
       r.room_code,
       r.building,
       se.day_of_week,
       se.period_no,
       ts.start_time,
       ts.end_time
FROM schedule_entries se
         JOIN teaching_classes tc ON tc.id = se.teaching_class_id
         JOIN courses          c  ON c.id = se.course_id
         JOIN teachers         t  ON t.id = se.teacher_id
         JOIN classrooms       r  ON r.id = se.classroom_id
         JOIN time_slots       ts ON ts.day_of_week = se.day_of_week AND ts.period_no = se.period_no;

-- ---------------------------------------------------------------------------
-- 视图 3: 学生个人课表(学生端查课: 学生 -> 课程 -> 时段 -> 地点)
-- ---------------------------------------------------------------------------
CREATE OR REPLACE VIEW v_student_timetable AS
SELECT e.student_id,
       s.student_no,
       s.student_name,
       e.semester_code,
       e.status          AS enroll_status,
       e.score,
       tc.class_code,
       c.course_code,
       c.course_name,
       c.credits,
       t.teacher_name,
       r.room_code,
       r.building,
       se.day_of_week,
       se.period_no,
       ts.start_time,
       ts.end_time
FROM enrollments e
         JOIN students         s  ON s.id = e.student_id
         JOIN teaching_classes tc ON tc.id = e.teaching_class_id
         JOIN courses          c  ON c.id = tc.course_id
         JOIN teachers         t  ON t.id = tc.teacher_id
         LEFT JOIN schedule_entries se ON se.teaching_class_id = tc.id
         LEFT JOIN classrooms       r  ON r.id = se.classroom_id
         LEFT JOIN time_slots       ts ON ts.day_of_week = se.day_of_week AND ts.period_no = se.period_no
WHERE e.status <> 'dropped';

-- ---------------------------------------------------------------------------
-- 视图 4: 学期统计(排课结果概览)
-- ---------------------------------------------------------------------------
CREATE OR REPLACE VIEW v_semester_statistics AS
SELECT tc.semester_code,
       COUNT(DISTINCT tc.course_id)                        AS course_count,
       COUNT(DISTINCT tc.id)                               AS class_count,
       COUNT(DISTINCT se.id)                               AS schedule_entry_count,
       COUNT(DISTINCT CASE WHEN se.id IS NULL THEN tc.id END) AS unscheduled_class_count,
       COALESCE(SUM(DISTINCT c.credits), 0)                AS total_credits
FROM teaching_classes tc
         JOIN courses c ON c.id = tc.course_id
         LEFT JOIN schedule_entries se ON se.teaching_class_id = tc.id
GROUP BY tc.semester_code;

