-- ============================================================================
--  学生课表排课系统 —— 演示种子数据
--  文件: sql/02_seed_data.sql   (依赖 01_schema.sql)
--  数据集规模:
--    学期 3 个 / 教师 13 / 教室 9 / 节次 25 / 课程 27(DAG 顶点)
--    先修关系 33 条(DAG 边) / 教学班 39 / 学生 30 / 历史成绩记录若干
--  DAG 结构说明(部分关键链):
--    CS101 程序设计基础 -> CS201 数据结构与算法 -> CS204 数据库原理 -> CS305 数据库系统实现 -> CS311 云计算与大数据
--    CS102 离散数学     -> CS301 算法设计与分析   -> CS302 编译原理
--    CS202 计算机组成原理 -> CS205 操作系统 -> CS306 分布式系统 -> CS311
--    MA101 高等数学(上)  -> MA102 高等数学(下) / MA201 线性代数 / MA202 概率论与数理统计
--  该 DAG 最长路径 5 层, 用于验证 Kahn 算法分层输出与循环依赖检测。
-- ============================================================================
SET NAMES utf8mb4;

-- ---------------------------------------------------------------------------
-- 1. 学期
-- ---------------------------------------------------------------------------
INSERT INTO semesters (semester_code, semester_name, start_date, total_weeks, is_current) VALUES
('2025-2026-1', '2025-2026 学年第一学期', '2025-09-01', 16, 0),
('2025-2026-2', '2025-2026 学年第二学期', '2026-02-23', 16, 0),
('2026-2027-1', '2026-2027 学年第一学期', '2026-09-07', 16, 1);

-- ---------------------------------------------------------------------------
-- 2. 教师(13 人) 与 教室(9 间)
-- ---------------------------------------------------------------------------
INSERT INTO teachers (teacher_no, teacher_name, title, dept, max_classes) VALUES
('T001', '张伟', '教授',   '计算机学院',     4),
('T002', '李娜', '副教授', '计算机学院',     4),
('T003', '王强', '讲师',   '计算机学院',     4),
('T004', '陈静', '副教授', '计算机学院',     4),
('T005', '刘洋', '讲师',   '计算机学院',     4),
('T006', '赵敏', '教授',   '计算机学院',     4),
('T007', '孙磊', '讲师',   '计算机学院',     4),
('T008', '周慧', '副教授', '数学学院',       4),
('T009', '吴涛', '讲师',   '数学学院',       4),
('T010', '郑华', '副教授', '外国语学院',     4),
('T011', '冯霞', '讲师',   '外国语学院',     4),
('T012', '蒋峰', '讲师',   '马克思主义学院', 4),
('T013', '韩梅', '讲师',   '体育学院',       4);

INSERT INTO classrooms (room_code, building, capacity, room_type) VALUES
('A101', '教学楼A', 60,  'general'),
('A102', '教学楼A', 60,  'general'),
('A201', '教学楼A', 90,  'general'),
('B101', '教学楼B', 120, 'general'),
('B201', '教学楼B', 60,  'general'),
('B202', '教学楼B', 45,  'general'),
('C301', '机房楼C', 50,  'computer'),
('C302', '机房楼C', 50,  'computer'),
('L401', '实验楼',  40,  'lab');

-- ---------------------------------------------------------------------------
-- 3. 节次: 周一至周五, 每天 5 个大节 (共 25 个可用时段)
--    slot_weight 越小表示越优先占用(上午优先, 周五下午/晚上靠后)
-- ---------------------------------------------------------------------------
INSERT INTO time_slots (day_of_week, period_no, start_time, end_time, slot_weight) VALUES
(1, 1, '08:00:00', '09:40:00',   0), (1, 2, '10:00:00', '11:40:00',  20),
(1, 3, '14:00:00', '15:40:00',  40), (1, 4, '16:00:00', '17:40:00',  60),
(1, 5, '19:00:00', '20:40:00',  80),
(2, 1, '08:00:00', '09:40:00',   4), (2, 2, '10:00:00', '11:40:00',  24),
(2, 3, '14:00:00', '15:40:00',  44), (2, 4, '16:00:00', '17:40:00',  64),
(2, 5, '19:00:00', '20:40:00',  84),
(3, 1, '08:00:00', '09:40:00',   8), (3, 2, '10:00:00', '11:40:00',  28),
(3, 3, '14:00:00', '15:40:00',  48), (3, 4, '16:00:00', '17:40:00',  68),
(3, 5, '19:00:00', '20:40:00',  88),
(4, 1, '08:00:00', '09:40:00',  12), (4, 2, '10:00:00', '11:40:00',  32),
(4, 3, '14:00:00', '15:40:00',  52), (4, 4, '16:00:00', '17:40:00',  72),
(4, 5, '19:00:00', '20:40:00',  92),
(5, 1, '08:00:00', '09:40:00',  26), (5, 2, '10:00:00', '11:40:00',  46),
(5, 3, '14:00:00', '15:40:00',  66), (5, 4, '16:00:00', '17:40:00',  86),
(5, 5, '19:00:00', '20:40:00', 106);

-- ---------------------------------------------------------------------------
-- 4. 课程 (DAG 顶点, 27 门)
--    class_hours_per_week 由学分推导: 学分 >= 3.0 每周排 2 个大节, 否则 1 个大节
-- ---------------------------------------------------------------------------
INSERT INTO courses (course_code, course_name, dept, credits, total_hours, course_type, class_hours_per_week)
SELECT m.course_code, m.course_name, m.dept, m.credits, m.total_hours, m.course_type,
       CASE WHEN m.credits >= 3.0 THEN 2 ELSE 1 END
FROM (
              SELECT 'CS101' AS course_code, '程序设计基础' AS course_name, '计算机学院' AS dept, 4.0 AS credits, 64 AS total_hours, 'required' AS course_type
    UNION ALL SELECT 'CS102', '离散数学',         '计算机学院', 3.0, 48, 'required'
    UNION ALL SELECT 'CS201', '数据结构与算法',   '计算机学院', 4.0, 64, 'required'
    UNION ALL SELECT 'CS202', '计算机组成原理',   '计算机学院', 3.5, 56, 'required'
    UNION ALL SELECT 'CS203', '面向对象程序设计', '计算机学院', 3.0, 48, 'required'
    UNION ALL SELECT 'CS204', '数据库原理',       '计算机学院', 3.0, 48, 'required'
    UNION ALL SELECT 'CS205', '操作系统',         '计算机学院', 4.0, 64, 'required'
    UNION ALL SELECT 'CS206', '计算机网络',       '计算机学院', 3.5, 56, 'required'
    UNION ALL SELECT 'CS301', '算法设计与分析',   '计算机学院', 3.0, 48, 'required'
    UNION ALL SELECT 'CS302', '编译原理',         '计算机学院', 3.0, 48, 'required'
    UNION ALL SELECT 'CS303', '软件工程',         '计算机学院', 3.0, 48, 'required'
    UNION ALL SELECT 'CS304', '机器学习',         '计算机学院', 3.0, 48, 'elective'
    UNION ALL SELECT 'CS305', '数据库系统实现',   '计算机学院', 3.0, 48, 'elective'
    UNION ALL SELECT 'CS306', '分布式系统',       '计算机学院', 3.0, 48, 'elective'
    UNION ALL SELECT 'CS307', '人工智能导论',     '计算机学院', 2.5, 40, 'elective'
    UNION ALL SELECT 'CS308', 'Web 应用开发',     '计算机学院', 2.5, 40, 'elective'
    UNION ALL SELECT 'CS309', '信息安全基础',     '计算机学院', 2.5, 40, 'elective'
    UNION ALL SELECT 'CS310', '数据挖掘',         '计算机学院', 2.5, 40, 'elective'
    UNION ALL SELECT 'CS311', '云计算与大数据',   '计算机学院', 2.5, 40, 'elective'
    UNION ALL SELECT 'MA101', '高等数学(上)',     '数学学院',   5.0, 80, 'required'
    UNION ALL SELECT 'MA102', '高等数学(下)',     '数学学院',   5.0, 80, 'required'
    UNION ALL SELECT 'MA201', '线性代数',         '数学学院',   3.0, 48, 'required'
    UNION ALL SELECT 'MA202', '概率论与数理统计', '数学学院',   3.0, 48, 'required'
    UNION ALL SELECT 'EN101', '大学英语(I)',      '外国语学院', 2.0, 32, 'general'
    UNION ALL SELECT 'EN102', '大学英语(II)',     '外国语学院', 2.0, 32, 'general'
    UNION ALL SELECT 'PE101', '大学体育(I)',      '体育学院',   1.0, 32, 'general'
    UNION ALL SELECT 'GE101', '思想政治教育',     '马克思主义学院', 2.0, 32, 'general'
) m;

-- ---------------------------------------------------------------------------
-- 5. 课程先修关系 (DAG 边: prereq_course_id -> course_id, 共 33 条)
-- ---------------------------------------------------------------------------
INSERT INTO course_prerequisites (course_id, prereq_course_id)
SELECT c.id, p.id
FROM (
              SELECT 'MA102' AS course_code, 'MA101' AS prereq_code
    UNION ALL SELECT 'MA201', 'MA101'
    UNION ALL SELECT 'MA202', 'MA101'
    UNION ALL SELECT 'CS102', 'MA101'
    UNION ALL SELECT 'CS201', 'CS101'
    UNION ALL SELECT 'CS201', 'CS102'
    UNION ALL SELECT 'CS202', 'CS101'
    UNION ALL SELECT 'CS203', 'CS101'
    UNION ALL SELECT 'CS204', 'CS201'
    UNION ALL SELECT 'CS205', 'CS202'
    UNION ALL SELECT 'CS205', 'CS201'
    UNION ALL SELECT 'CS206', 'CS202'
    UNION ALL SELECT 'CS301', 'CS201'
    UNION ALL SELECT 'CS301', 'CS102'
    UNION ALL SELECT 'CS302', 'CS301'
    UNION ALL SELECT 'CS303', 'CS201'
    UNION ALL SELECT 'CS304', 'MA202'
    UNION ALL SELECT 'CS304', 'CS201'
    UNION ALL SELECT 'CS305', 'CS204'
    UNION ALL SELECT 'CS305', 'CS205'
    UNION ALL SELECT 'CS306', 'CS205'
    UNION ALL SELECT 'CS306', 'CS206'
    UNION ALL SELECT 'CS307', 'CS201'
    UNION ALL SELECT 'CS307', 'MA202'
    UNION ALL SELECT 'CS308', 'CS204'
    UNION ALL SELECT 'CS308', 'CS203'
    UNION ALL SELECT 'CS309', 'CS206'
    UNION ALL SELECT 'CS309', 'CS102'
    UNION ALL SELECT 'CS310', 'CS304'
    UNION ALL SELECT 'CS310', 'CS204'
    UNION ALL SELECT 'CS311', 'CS306'
    UNION ALL SELECT 'CS311', 'CS204'
    UNION ALL SELECT 'EN102', 'EN101'
) m
JOIN courses c ON c.course_code = m.course_code
JOIN courses p ON p.course_code = m.prereq_code;

-- ---------------------------------------------------------------------------
-- 6. 教学班
--    6.1 历史学期教学班(只为"学生先修课程是否已通过"提供成绩载体)
-- ---------------------------------------------------------------------------
INSERT INTO teaching_classes (class_code, course_id, teacher_id, semester_code, class_name, capacity, expected_students, open_for_enroll)
SELECT m.class_code, c.id, t.id, m.semester_code, m.class_name, m.capacity, m.expected_students, 0
FROM (
              SELECT 'CS101-H1' AS class_code, 'CS101' AS course_code, 'T003' AS teacher_no, '2025-2026-1' AS semester_code, '程序设计基础 · 2025秋教学班' AS class_name, 60 AS capacity, 58 AS expected_students
    UNION ALL SELECT 'MA101-H1', 'MA101', 'T008', '2025-2026-1', '高等数学(上) · 2025秋教学班',  90, 88
    UNION ALL SELECT 'EN101-H1', 'EN101', 'T010', '2025-2026-1', '大学英语(I) · 2025秋教学班',   60, 58
    UNION ALL SELECT 'PE101-H1', 'PE101', 'T013', '2025-2026-1', '大学体育(I) · 2025秋教学班',   60, 60
    UNION ALL SELECT 'GE101-H1', 'GE101', 'T012', '2025-2026-1', '思想政治教育 · 2025秋教学班',  90, 88
    UNION ALL SELECT 'MA102-H2', 'MA102', 'T008', '2025-2026-2', '高等数学(下) · 2026春教学班',  90, 85
    UNION ALL SELECT 'CS102-H2', 'CS102', 'T001', '2025-2026-2', '离散数学 · 2026春教学班',      90, 85
    UNION ALL SELECT 'CS201-H2', 'CS201', 'T001', '2025-2026-2', '数据结构与算法 · 2026春教学班', 60, 56
    UNION ALL SELECT 'CS203-H2', 'CS203', 'T005', '2025-2026-2', '面向对象程序设计 · 2026春教学班', 60, 55
) m
JOIN courses  c ON c.course_code = m.course_code
JOIN teachers t ON t.teacher_no  = m.teacher_no;

-- ---------------------------------------------------------------------------
--    6.2 当前学期教学班(2026-2027-1, 共 30 个, 是排课/选课的主角)
--        expected_students 用于排课时选择容量合适的教室; 大班课程需要大教室,
--        因此这一步会产生真实的"教室资源竞争", 用于体现排课算法的价值。
-- ---------------------------------------------------------------------------
INSERT INTO teaching_classes (class_code, course_id, teacher_id, semester_code, class_name, capacity, expected_students, open_for_enroll)
SELECT m.class_code, c.id, t.id, '2026-2027-1', m.class_name, m.capacity, m.expected_students, 1
FROM (
              SELECT 'CS101-01' AS class_code, 'CS101' AS course_code, 'T003' AS teacher_no, '程序设计基础 · 教学班01' AS class_name, 60 AS capacity, 55 AS expected_students
    UNION ALL SELECT 'CS102-01', 'CS102', 'T001', '离散数学 · 教学班01',           90, 80
    UNION ALL SELECT 'CS201-01', 'CS201', 'T001', '数据结构与算法 · 教学班01',     60, 58
    UNION ALL SELECT 'CS201-02', 'CS201', 'T002', '数据结构与算法 · 教学班02',     60, 52
    UNION ALL SELECT 'CS202-01', 'CS202', 'T004', '计算机组成原理 · 教学班01',     60, 55
    UNION ALL SELECT 'CS203-01', 'CS203', 'T005', '面向对象程序设计 · 教学班01',   60, 56
    UNION ALL SELECT 'CS203-02', 'CS203', 'T003', '面向对象程序设计 · 教学班02',   45, 40
    UNION ALL SELECT 'CS204-01', 'CS204', 'T002', '数据库原理 · 教学班01',         60, 58
    UNION ALL SELECT 'CS204-02', 'CS204', 'T006', '数据库原理 · 教学班02',         60, 50
    UNION ALL SELECT 'CS205-01', 'CS205', 'T006', '操作系统 · 教学班01',           60, 55
    UNION ALL SELECT 'CS206-01', 'CS206', 'T004', '计算机网络 · 教学班01',         60, 52
    UNION ALL SELECT 'CS301-01', 'CS301', 'T001', '算法设计与分析 · 教学班01',     50, 45
    UNION ALL SELECT 'CS302-01', 'CS302', 'T007', '编译原理 · 教学班01',           45, 38
    UNION ALL SELECT 'CS303-01', 'CS303', 'T005', '软件工程 · 教学班01',           60, 52
    UNION ALL SELECT 'CS304-01', 'CS304', 'T002', '机器学习 · 教学班01',           50, 45
    UNION ALL SELECT 'CS305-01', 'CS305', 'T004', '数据库系统实现 · 教学班01',     45, 40
    UNION ALL SELECT 'CS306-01', 'CS306', 'T007', '分布式系统 · 教学班01',         45, 36
    UNION ALL SELECT 'CS307-01', 'CS307', 'T006', '人工智能导论 · 教学班01',       50, 44
    UNION ALL SELECT 'CS308-01', 'CS308', 'T005', 'Web 应用开发 · 教学班01',       50, 45
    UNION ALL SELECT 'CS309-01', 'CS309', 'T007', '信息安全基础 · 教学班01',       45, 40
    UNION ALL SELECT 'CS310-01', 'CS310', 'T002', '数据挖掘 · 教学班01',           45, 35
    UNION ALL SELECT 'CS311-01', 'CS311', 'T004', '云计算与大数据 · 教学班01',     45, 32
    UNION ALL SELECT 'MA101-01', 'MA101', 'T008', '高等数学(上) · 教学班01',       90, 88
    UNION ALL SELECT 'MA102-01', 'MA102', 'T008', '高等数学(下) · 教学班01',       90, 78
    UNION ALL SELECT 'MA201-01', 'MA201', 'T009', '线性代数 · 教学班01',           60, 55
    UNION ALL SELECT 'MA202-01', 'MA202', 'T009', '概率论与数理统计 · 教学班01',   60, 50
    UNION ALL SELECT 'EN101-01', 'EN101', 'T010', '大学英语(I) · 教学班01',        60, 58
    UNION ALL SELECT 'EN102-01', 'EN102', 'T011', '大学英语(II) · 教学班01',       60, 45
    UNION ALL SELECT 'PE101-01', 'PE101', 'T013', '大学体育(I) · 教学班01',        60, 60
    UNION ALL SELECT 'GE101-01', 'GE101', 'T012', '思想政治教育 · 教学班01',       90, 88
) m
JOIN courses  c ON c.course_code = m.course_code
JOIN teachers t ON t.teacher_no  = m.teacher_no;

-- ---------------------------------------------------------------------------
-- 7. 学生 (30 人: 大三 10 / 大二 10 / 大一 10)
--    口令摘要 = SHA2(CONCAT(学号, '#', 明文口令), 256), 演示口令统一为 123456
-- ---------------------------------------------------------------------------
INSERT INTO students (student_no, student_name, gender, grade_year, major, admin_class, dept, status, password_hash) VALUES
('2024001', '张明', 'M', 2024, '计算机科学与技术', '计科2401', '计算机学院', 'active', SHA2(CONCAT('2024001#123456'), 256)),
('2024002', '李思', 'F', 2024, '计算机科学与技术', '计科2401', '计算机学院', 'active', SHA2(CONCAT('2024002#123456'), 256)),
('2024003', '王芳', 'F', 2024, '计算机科学与技术', '计科2401', '计算机学院', 'active', SHA2(CONCAT('2024003#123456'), 256)),
('2024004', '陈晨', 'M', 2024, '计算机科学与技术', '计科2401', '计算机学院', 'active', SHA2(CONCAT('2024004#123456'), 256)),
('2024005', '刘洋', 'M', 2024, '计算机科学与技术', '计科2401', '计算机学院', 'active', SHA2(CONCAT('2024005#123456'), 256)),
('2024006', '赵磊', 'M', 2024, '软件工程',         '软工2401', '计算机学院', 'active', SHA2(CONCAT('2024006#123456'), 256)),
('2024007', '孙悦', 'F', 2024, '软件工程',         '软工2401', '计算机学院', 'active', SHA2(CONCAT('2024007#123456'), 256)),
('2024008', '周涛', 'M', 2024, '软件工程',         '软工2401', '计算机学院', 'active', SHA2(CONCAT('2024008#123456'), 256)),
('2024009', '吴静', 'F', 2024, '软件工程',         '软工2401', '计算机学院', 'active', SHA2(CONCAT('2024009#123456'), 256)),
('2024010', '郑凯', 'M', 2024, '软件工程',         '软工2401', '计算机学院', 'active', SHA2(CONCAT('2024010#123456'), 256)),
('2025001', '冯雪', 'F', 2025, '计算机科学与技术', '计科2501', '计算机学院', 'active', SHA2(CONCAT('2025001#123456'), 256)),
('2025002', '蒋鹏', 'M', 2025, '计算机科学与技术', '计科2501', '计算机学院', 'active', SHA2(CONCAT('2025002#123456'), 256)),
('2025003', '韩雨', 'F', 2025, '计算机科学与技术', '计科2501', '计算机学院', 'active', SHA2(CONCAT('2025003#123456'), 256)),
('2025004', '曹阳', 'M', 2025, '计算机科学与技术', '计科2501', '计算机学院', 'active', SHA2(CONCAT('2025004#123456'), 256)),
('2025005', '邓超', 'M', 2025, '计算机科学与技术', '计科2501', '计算机学院', 'active', SHA2(CONCAT('2025005#123456'), 256)),
('2025006', '谢婷', 'F', 2025, '软件工程',         '软工2501', '计算机学院', 'active', SHA2(CONCAT('2025006#123456'), 256)),
('2025007', '罗敏', 'F', 2025, '软件工程',         '软工2501', '计算机学院', 'active', SHA2(CONCAT('2025007#123456'), 256)),
('2025008', '唐磊', 'M', 2025, '软件工程',         '软工2501', '计算机学院', 'active', SHA2(CONCAT('2025008#123456'), 256)),
('2025009', '曾毅', 'M', 2025, '软件工程',         '软工2501', '计算机学院', 'active', SHA2(CONCAT('2025009#123456'), 256)),
('2025010', '彭浩', 'M', 2025, '软件工程',         '软工2501', '计算机学院', 'active', SHA2(CONCAT('2025010#123456'), 256)),
('2026001', '吕佳', 'F', 2026, '计算机科学与技术', '计科2601', '计算机学院', 'active', SHA2(CONCAT('2026001#123456'), 256)),
('2026002', '苏航', 'M', 2026, '计算机科学与技术', '计科2601', '计算机学院', 'active', SHA2(CONCAT('2026002#123456'), 256)),
('2026003', '卢欣', 'F', 2026, '计算机科学与技术', '计科2601', '计算机学院', 'active', SHA2(CONCAT('2026003#123456'), 256)),
('2026004', '蔡明', 'M', 2026, '计算机科学与技术', '计科2601', '计算机学院', 'active', SHA2(CONCAT('2026004#123456'), 256)),
('2026005', '贾斌', 'M', 2026, '计算机科学与技术', '计科2601', '计算机学院', 'active', SHA2(CONCAT('2026005#123456'), 256)),
('2026006', '丁宁', 'F', 2026, '软件工程',         '软工2601', '计算机学院', 'active', SHA2(CONCAT('2026006#123456'), 256)),
('2026007', '魏然', 'M', 2026, '软件工程',         '软工2601', '计算机学院', 'active', SHA2(CONCAT('2026007#123456'), 256)),
('2026008', '薛冰', 'F', 2026, '软件工程',         '软工2601', '计算机学院', 'active', SHA2(CONCAT('2026008#123456'), 256)),
('2026009', '侯军', 'M', 2026, '软件工程',         '软工2601', '计算机学院', 'active', SHA2(CONCAT('2026009#123456'), 256)),
('2026010', '白露', 'F', 2026, '软件工程',         '软工2601', '计算机学院', 'active', SHA2(CONCAT('2026010#123456'), 256));

-- ---------------------------------------------------------------------------
-- 8. 历史成绩(构成每位学生的"先修完成情况", 是选课先修校验的数据来源)
--    8.1 2025-2026-1 学期: 大二/大三学生完成公共基础课与程序设计基础
-- ---------------------------------------------------------------------------
INSERT INTO enrollments (student_id, teaching_class_id, semester_code, status, score)
SELECT s.id, tc.id, tc.semester_code, 'completed', 65 + (s.id * 7 + tc.id * 5) % 35
FROM students s
         JOIN teaching_classes tc ON tc.semester_code = '2025-2026-1'
WHERE s.grade_year <= 2025;

-- 8.2 2025-2026-2 学期: 大二/大三学生学习离散数学与高等数学(下),
--     大三学生另修《数据结构与算法》《面向对象程序设计》;
--     其中 id 为 4 的倍数的学生《数据结构与算法》不及格(failed),
--     用于演示学生选课被"先修课未通过"拦截的场景。
INSERT INTO enrollments (student_id, teaching_class_id, semester_code, status, score)
SELECT s.id, tc.id, tc.semester_code,
       CASE WHEN tc.class_code = 'CS201-H2' AND s.id % 4 = 0 THEN 'failed' ELSE 'completed' END,
       CASE WHEN tc.class_code = 'CS201-H2' AND s.id % 4 = 0 THEN 52.00
            ELSE 66 + (s.id * 11 + tc.id * 3) % 33 END
FROM students s
         JOIN teaching_classes tc
              ON tc.semester_code = '2025-2026-2'
                  AND (tc.class_code IN ('MA102-H2', 'CS102-H2')
                           OR (s.grade_year = 2024 AND tc.class_code IN ('CS201-H2', 'CS203-H2')))
WHERE s.grade_year <= 2025;

-- ---------------------------------------------------------------------------
-- 9. 当前学期预置选课(3 名学生各选 2 门)
--    用途: 让"学生查课"打开就有数据, 并为"时间冲突拦截"提供可复现场景
-- ---------------------------------------------------------------------------
INSERT INTO enrollments (student_id, teaching_class_id, semester_code, status, score)
SELECT s.id, tc.id, '2026-2027-1', 'enrolled', NULL
FROM students s
         JOIN teaching_classes tc ON tc.class_code IN ('CS201-01', 'CS202-01')
WHERE s.student_no IN ('2024001', '2024002', '2025001');

-- ---------------------------------------------------------------------------
-- 10. 校正教学班已选人数(与 enrollments 保持一致)
-- ---------------------------------------------------------------------------
UPDATE teaching_classes tc
SET tc.enrolled_count = (SELECT COUNT(*)
                         FROM enrollments e
                         WHERE e.teaching_class_id = tc.id
                           AND e.status IN ('enrolled', 'completed'));



