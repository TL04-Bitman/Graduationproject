# 数据库设计（DATABASE）

> 目标库：MySQL 8.0，字符集 `utf8mb4` / 排序规则 `utf8mb4_0900_ai_ci`，全部使用 InnoDB。
> 结构定义见 [`sql/01_schema.sql`](../sql/01_schema.sql)，演示数据见 [`sql/02_seed_data.sql`](../sql/02_seed_data.sql)。

---

## 1. ER 概览

```
        ┌────────────┐
        │ semesters  │ 学期(2025-2026-1 / 2026-2027-1 ...)
        └─────┬──────┘
              │ semester_code(冗余到教学班/排课/选课, 便于按期过滤)
              ▼
┌──────────────┐        ┌────────────────────────┐
│   courses    │◄──────►│ course_prerequisites    │  DAG 的边: prereq_course_id → course_id
│ (DAG 顶点)   │  1:N   │ UNIQUE(course_id,pre)   │
└──────┬───────┘        │ CHECK(course_id<>pre)   │  ← 拦住自环
       │ 1:N            └────────────────────────┘
       ▼
┌────────────────────┐   N:1   ┌──────────┐        ┌─────────────┐
│ teaching_classes   │────────►│ teachers │        │ classrooms  │
│ (排课/选课最小单位) │         └──────────┘        └──────┬──────┘
└────────┬───────────┘                                     │
         │ 1:N                                             │ N:1
         ▼                                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│ schedule_entries  (教学班每周固定时段 + 教室; 冗余 teacher_id)     │
│ UNIQUE(teaching_class_id, semester, day, period)  ← 班级不重复     │
│ UNIQUE(teacher_id, semester, day, period)         ← 教师不冲突     │
│ UNIQUE(classroom_id, semester, day, period)       ← 教室不冲突     │
└─────────────────────────────────────────────────────────────────┘

┌──────────┐  1:N   ┌────────────────────┐  N:1   teaching_classes
│ students │───────►│    enrollments     │────────┘
└──────────┘        │ (选课 + 历史成绩)   │ status: enrolled/completed/failed/dropped
                    └────────────────────┘
┌────────────────────┐
│ scheduling_runs    │ 每次排课的审计记录(耗时/成功数/算法/失败原因)
└────────────────────┘
```

---

## 2. 表清单

| 表 | 作用 | 关键字段 | 演示数据量 |
| --- | --- | --- | --- |
| `semesters` | 学期 | `semester_code`, `is_current` | 3 |
| `courses` | 课程（DAG 顶点） | `course_code`, `credits`, `class_hours_per_week` | 27 |
| `course_prerequisites` | 先修关系（DAG 边） | `course_id`, `prereq_course_id` | 33 |
| `teachers` | 教师 | `teacher_no`, `max_classes`（工作量上限） | 13 |
| `classrooms` | 教室 | `room_code`, `capacity`, `room_type` | 9 |
| `time_slots` | 节次（5 天 × 5 大节 = 25 个时段） | `day_of_week`, `period_no`, `slot_weight` | 25 |
| `teaching_classes` | 教学班（某学期某课程的一次开课） | `class_code`, `semester_code`, `expected_students` | 39（本学期 30） |
| `schedule_entries` | 排课结果（周固定时段） | `teaching_class_id`, `classroom_id`, `day_of_week` | 排课后 51 |
| `students` | 学生（登录账号） | `student_no`, `password_hash` | 30 |
| `enrollments` | 选课 + 成绩 | `student_id`, `teaching_class_id`, `status`, `score` | 历史 + 本学期 |
| `scheduling_runs` | 排课批次审计 | `status`, `duration_ms`, `failed_classes` | 每次排课 1 条 |

### 设计要点

1. **`course_prerequisites` 只存边**，不存"层级/深度"等可由算法推导的信息 ——
   层级由 Kahn 之后一次 BFS 实时计算（`compute_levels()`），避免冗余字段引发不一致。
2. **`semester_code` 用业务编码字符串**（如 `2026-2027-1`）并冗余到教学班/排课/选课表，
   便于按期过滤；`semesters` 表保留学期元数据（起止日期、周数）。
3. **`enrollments` 同时承担"选课"与"历史成绩"**：先修是否满足 = 该生在此表中存在
   `status='completed'` 且 `score >= MIN_SCORE_TO_PASS` 的记录。
4. **`schedule_entries` 冗余 `teacher_id` / `course_id`**：这是让"教师时段唯一""教室时段唯一"
   能用**唯一索引**在数据库层兜底的前提（见第 4 节）。该表只由排课流程一次性写入，不存在更新导致的不同步。
5. **`enrolled_count` 是冗余计数**，选课/退课后由 `refresh_class_enrolled()` 校正，
   也可用 `TeachingRepository::sync_enrolled_counts()` 全量重建。

---

## 3. 视图（把复杂 JOIN 固化在库侧）

| 视图 | 用途 | 使用方 |
| --- | --- | --- |
| `v_prereq_edges` | 先修关系（边）带课程编号与名称 | 教学计划展示 |
| `v_class_timetable` | 教学班 → 时段 → 教师/教室 | 全校课表查询 |
| `v_student_timetable` | 学生 → 已选课程 → 时段/地点/成绩 | **学生查课**（`StudentRepository::timetable`） |
| `v_semester_statistics` | 学期维度统计（课程数/班级数/排课记录/未排班级） | 统计页 |

学生课表的核心查询因此简化为：

```sql
SELECT course_code, course_name, class_code, teacher_name, room_code, building,
       credits, day_of_week, period_no, enroll_status, score
FROM v_student_timetable
WHERE student_id = ? AND semester_code = ?
ORDER BY day_of_week IS NULL, day_of_week, period_no;
```

> 注意：视图中的选课状态列名为 `enroll_status`（避免与 `students.status` 混淆）。

---

## 4. 索引与约束（防冲突的第二道防线）

### 唯一索引 —— 即使应用层有 bug，冲突数据也写不进库

| 索引 | 表 / 列 | 语义 |
| --- | --- | --- |
| `uk_class_slot` | `schedule_entries(teaching_class_id, semester_code, day_of_week, period_no)` | 同一教学班同一时段只能排一次 |
| `uk_teacher_slot` | `schedule_entries(teacher_id, semester_code, day_of_week, period_no)` | 同一教师同一时段不能出现在两个教室 |
| `uk_room_slot` | `schedule_entries(classroom_id, semester_code, day_of_week, period_no)` | 同一教室同一时段不能安排两个班 |
| `uk_course_prereq` | `course_prerequisites(course_id, prereq_course_id)` | 同一条先修关系不重复 |
| `uk_student_class` | `enrollments(student_id, teaching_class_id, semester_code)` | 同一学生同一学期同一班只选一次 |
| `uk_class_code` / `uk_course_code` / `uk_room_code` / `uk_teacher_no` / `uk_student_no` | 各主数据表 | 业务编号唯一 |

### 其它约束

- `CHECK (course_id <> prereq_course_id)`：**拦住自环**（自环必然成环，写入阶段直接拒绝）。
- 全表外键保证引用完整性；`enrollments`、`schedule_entries` 对 `teaching_classes`
  使用 `ON DELETE CASCADE`（删教学班时一并清理其排课与选课记录），其余为默认 `RESTRICT`。
- `ENUM` 使用英文码（`required/elective/general`、`enrolled/completed/failed/dropped`），
  中文展示在应用层完成（`types.cpp` 的 `label()`），避免存储与展示耦合。

### 常用查询索引

`idx_tc_semester`、`idx_enroll_student(student_id, semester_code)`、`idx_enroll_class`、
`idx_se_semester`、`idx_prereq_course`、`idx_courses_type` 等，覆盖按学期/按学生/按班级的高频过滤。

---

## 5. 演示数据说明（`02_seed_data.sql`）

- **课程与先修**：27 门课程，覆盖计算机 / 数学 / 外语 / 体育 / 思政；先修链最深 5 层，
  含"一门课多先修"（如 CS205 ← CS202 + CS201）与"一先修多后继"（如 MA101 → 4 门课）。
- **教学班**：历史学期 9 个（作为学生先修成绩的载体）+ 当前学期 30 个
  （含 3 门热门课程各两个班，用于演示"同一课程多个教学班"）。
- **资源**：13 名教师、9 间教室（含机房、实验室）、25 个时段；构造出真实的资源竞争
  （例如 85 人以上的大班只能进 2 间大教室）。
- **学生**：30 人（大三 / 大二 / 大一各 10 人），历史成绩按学期递进；其中 id 为 4 的倍数的学生
  《数据结构与算法》**不及格**，用于演示"先修未通过"拦截。
- **预置选课**：3 名学生各选 2 门，保证学生端一打开就有课表，并为"时间冲突"提供场景。

重建数据（两种方式等价）：

```bash
./build/scheduler init-db            # 命令行
# 或界面: 主菜单 → 5 系统信息 → 3 初始化/重建演示数据
```

---

## 6. 手工排查用的 SQL 片段

```sql
-- 1) 检查排课结果是否有冲突(正常应返回 0 行)
SELECT teacher_id, semester_code, day_of_week, period_no, COUNT(*) AS c
FROM schedule_entries GROUP BY 1,2,3,4 HAVING c > 1;

-- 2) 某个学生的课表(视图)
SELECT * FROM v_student_timetable
WHERE student_no = '2024001' ORDER BY day_of_week, period_no;

-- 3) 某门课的直接先修
SELECT c.course_code AS course, pc.course_code AS prereq
FROM course_prerequisites p
JOIN courses c  ON c.id  = p.course_id
JOIN courses pc ON pc.id = p.prereq_course_id
WHERE c.course_code = 'CS311';

-- 4) 学生的先修完成情况(是否及格)
SELECT c.course_code, e.status, e.score
FROM enrollments e
JOIN teaching_classes tc ON tc.id = e.teaching_class_id
JOIN courses c ON c.id = tc.course_id
WHERE e.student_id = (SELECT id FROM students WHERE student_no = '2024001')
ORDER BY c.course_code;

-- 5) 最近 10 次排课审计
SELECT id, semester_code, algorithm, duration_ms, total_classes,
       scheduled_entries, failed_classes, status
FROM scheduling_runs ORDER BY id DESC LIMIT 10;

-- 6) 教室使用率
SELECT r.room_code, r.capacity, COUNT(se.id) AS used_slots
FROM classrooms r
LEFT JOIN schedule_entries se ON se.classroom_id = r.id AND se.semester_code = '2026-2027-1'
GROUP BY r.id ORDER BY used_slots DESC;
```
