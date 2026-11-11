# IoT Shell V1.1 — 需求完善与决议

| 项 | 内容 |
|---|---|
| 文档类型 | 需求评审记录 + 修订决议（增量） |
| 基线 | 《IoT Shell 说明文档 V1.0》 |
| 版本 | V1.1（Draft for Review） |
| 日期 | 2026-11-10 |
| 配套文档 | `IoT-Shell-V1.1-规格说明.md`（自洽完整规格，可直接作为开发依据） |
| 适用范围 | Shell 层 / VM 层 / RTOS 任务层 / 设备层 / 安全链路 |

> **本文回答的问题**：V1.0 有哪些地方不能直接开工？12.2 的 7 个待定项怎么定？改了什么、为什么改、怎么验收？
> **配套规格回答的问题**：改完之后，完整规格长什么样？

---

## 0. 结论摘要

V1.0 的三层模型（Task / JOB / 设备）和"每个 JOB 独立 RTOS Task"的执行模型是**成立且正确的**，主体结构无需推翻。问题集中在**边界定义**上：解析上下文没有分表、命令与脚本关键字同名、JOB 生命周期缺状态与清理阶段、设备层只有名字没有规格、以及若干示例与正文互相打架。

| 统计 | 数量 |
|---|---|
| 阻断级问题（P0，不解决无法开工） | 7 |
| 歧义级问题（P1，实现者会各自猜） | 22 |
| 缺失级问题（P2，机制/章节缺失） | 11 |
| 合计 | **40** |
| 关闭的待定项 | **7 / 7** |
| 新增命令 | 7 |
| 新增脚本关键字 | 8 |
| 新增错误码 | 10 |
| 相对 V1.0 的破坏性变更 | 7 |

**三条最关键的修复：**

1. **引入双上下文分派**（决议 R-01）：`SHELL_CTX` 与 `VM_CTX` 各有一张解析表，`WAIT` / `START` / `STOP` / `SET` / `PRINT` 的同名冲突由此彻底消除。这是 V1.0 最致命的漏洞——按 1.4 的判定顺序，脚本里的 `WAIT TIME 100ms` 会被当成 Shell 的 `wait` 命令。
2. **补齐 JOB 生命周期**：新增 `load` 系列命令与 `LOADED` / `STOPPING` 状态，明确"停止不是错误"，新增 `ON STOP` 钩子与内核级设备安全收尾阶段。V1.0 里 `stop` 走 `ON ERROR`（1008）却以 `ABORTED` 结束，逻辑上无法自洽。
3. **定义前台 / 后台**：`run` 默认前台，`&` 为后台。V1.0 第 11 章 `run mix.ms` 之后立即回到 `>` 且 JOB 仍在 RUNNING，与 2.2 / 9.3 的 Ctrl-C 描述直接矛盾。

此外，运动控制系统不能只写软件层规格：**急停链路、限位、JOB 崩溃后电机默认状态、WAIT 不得忙等**已作为独立于 Shell 的硬约束补入（决议 R-19）。

---

## 1. 审阅口径

从五个维度逐章核对，每条问题都标注原文档位置：

| 维度 | 检查内容 |
|---|---|
| 一致性 | 正文、表格、示例、错误码表之间是否互相矛盾 |
| 完备性 | 被引用但未定义的命令 / 关键字 / 状态 / 变量是否存在 |
| 可实现性 | 给定规格，两个工程师能否写出行为一致的实现 |
| 安全性 | 运动控制场景下，异常路径是否会导致设备失控 |
| 可测试性 | 每条规则能否转成一条断言明确的用例 |

**判定原则**：凡"两个实现者会做出不同选择"的地方，一律视为缺陷，而不是留给实现者自由裁量。

---

## 2. 阻断级问题（P0）

### 2.1 清单

| 编号 | 位置 | 问题 | 决议 | 落点 |
|---|---|---|---|---|
| P0-01 | 1.4 / 2.1 / 6 / 10.5 | `WAIT` `START` `STOP` `SET` `UNSET` `PRINT` 既是 Shell 命令又是脚本关键字，1.4 只有单一查表顺序，脚本语句会被 Shell 命令截获 | **R-01** 双上下文分派 | 规格 §1.4 / §1.5 |
| P0-02 | 4.3 / 10.1 / 10.3 | `start` 要求 JOB 已 `load`，但全表无 `load` 命令、状态机无 `LOADED` 态 | **R-02** 补齐 `load` / `unload` / `loads`，状态机增加 `LOADED` | 规格 §4.2 / §4.3 |
| P0-03 | 2.2 / 9.3 / 11 | `run` 是前台阻塞还是后台并发未定义；9.3 出现未定义的"前台 JOB"概念 | **R-04** `run` 默认前台，`&` 后台，新增 `fg` | 规格 §2.5 / §9.3 |
| P0-04 | 3.3 / 4.3 / 7.6 / 11 | `confirm` 在 `kill` 是危险确认、在 `stop` 是"升级为强制"、在示例里又用于非危险对象 | **R-05** `confirm` 语义唯一化；强制停止改用 `force` | 规格 §3.4 / §4.4 / §7.6 |
| P0-05 | 3.3 / 4.3 / 4.4 / 11 | `stop` 的终止状态、`ON ERROR` 触发、cleanup 三者互相矛盾；示例里 `^C` 后执行了 `STEP 0 STOP, DEC 500ms` 却无规格定义 | **R-03** 完整状态机 + `ON STOP` + 内核 cleanup | 规格 §4.4 / §6.7 |
| P0-06 | 1.1 / 3.5 / 11 | JOB 名与承载 Task 名同名，但唯一性、重名、与系统任务同名、大小写等价、同脚本运行两次均未定义 | **R-06** `job_id` 为主键 + 名称折叠 + 保留名 + 冲突拒绝 | 规格 §3.5 / §4.1 |
| P0-07 | 1.1 / 3.3 / 4.4 / 10.5 / 11 | 设备层只有名字与示例语句，无语法、无占用/共享/释放规则；而 `kill` / JOB 结束都依赖"释放设备" | **R-10** 新增设备注册表章节 | 规格 §12 |

### 2.2 关键决议详述

#### R-01 · 双上下文分派（最高优先级）

V1.0 §1.4 的判定顺序隐含"所有输入共用一张命令表"，这是脚本无法工作的根因。V1.1 定义两个互斥上下文：

| 上下文 | 何时 | 第一 token 查表顺序 |
|---|---|---|
| `SHELL_CTX` | 交互主提示符 `>`、粘贴态的每个顶层行、非续行态 | 基础命令 → 扩展命令 → 脚本语句（转交互 JOB） |
| `VM_CTX` | JOB Task 内、续行块内（栈非空） | 脚本语句 → `SHELL <sub>` 内建 → 报错 |

配套规则：

- 同名词义由上下文唯一确定，不做参数猜测。例如 `WAIT TIME 100ms` 在 `VM_CTX` 是脚本等待，在 `SHELL_CTX` 会被解析为 Shell `wait` 并因 `TIME` 非法而报 `ERR 1001`——这是**期望行为**，需显式写进验收用例。
- VM 内需要 Shell 能力时用 `SHELL <sub>` 前缀（V1.0 已有 `SHELL PRINT` / `SHELL LOG` / `SHELL STATE`）。新增 `SHELL <cmd>` 透传，但**仅限白名单**（无副作用、可重入、不操作 JOB 生命周期的命令，如 `mem` / `ver` / `ps`）。透传 `kill` / `reboot` / `stop` 一律拒绝。
- VM 内 `START` / `STOP` 必须显式带 `JOB` 关键字：`START JOB FEED` / `STOP JOB FEED`，禁止裸 `START FEED`。Shell 层则保持 `start FEED` / `stop FEED`。这条同时消解了 §12.1 提到的"脚本内 START JOB"与"Shell 层 start"的历史重叠。

#### R-03 · 停止不是错误

V1.0 让协作式 `stop` 触发 `ON ERROR`（1008）却以 `ABORTED` 收尾，语义上是把用户主动停止当成程序故障。V1.1 拆开两条路径：

| 触发 | 脚本钩子 | 结束状态 | 内核 cleanup |
|---|---|---|---|
| 脚本运行时错误（除零、限位、设备失败…） | `ON ERROR`（置 `$ERRCODE` 等） | `ERROR` | 执行 |
| `stop <name>`（协作式） | `ON STOP` | `ABORTED` | 执行 |
| `stop <name> force` | `ON STOP`，若未定义则退化为 `ON ERROR`（兼容） | `ABORTED` | 执行 |
| `kill <task>` | **不执行任何钩子** | `ABORTED` | **仍执行** |
| `ENDJOB` 正常结束 | 可选 `ON STOP`（缺省无） | `COMPLETED` | 执行 |

关键不变量：**设备安全收尾（STEP 减速停 / PWM 关断 / GPIO 回到安全电平）由内核在 Task 删除前强制执行，不依赖脚本是否定义了钩子、也不依赖 JOB 是被 stop 还是 kill**。这既是 §11 示例里 `^C → STEP 0 STOP, DEC 500ms` 的规格化，也是安全底线。

#### R-04 · 前台 / 后台

| 形式 | Shell Task | Ctrl-C | 输出 |
|---|---|---|---|
| `run feed.ms` | 阻塞，等待该 JOB 结束 | 发 `stop` 给该 JOB | `[JOB FEED] ...` |
| `run feed.ms &` | 立即回到 `>` | 清空当前行（不影响后台 JOB） | `[JOB FEED] ...` |
| `start FEED` | 立即回到 `>` | 同上 | 同上 |
| `fg FEED` | 阻塞并附着到指定后台 JOB | 发 `stop` | 同上 |

"前台 JOB"由此定义为**当前附着到 Shell Task 的那一个 JOB**，全局最多一个，与"每个 JOB 独立 RTOS Task"不冲突——前台只是 Shell 的注意力归属，不改变调度模型。

#### R-06 · 命名唯一性

- 内部主键为单调递增的 `job_id`（**永不复用**）；Task 名、JOB 名只是显示与查找用的别名。
- 所有标识符按大小写折叠后比较：`FEED` ≡ `feed`。第二次 `run feed.ms` 报 `ERR 1018: job FEED already running`，需 `run feed.ms as FEED2`。
- 保留名集合 `shell` / `idle` / `timer` / `uart_rx` 及 RTOS 实际系统任务名，不可用作 JOB 名（否则 `ps` 与 `job` 无法区分）。
- `ps job` / `ps sys` / `ps all` 为保留选项；若确实存在同名任务，用 `ps name <name>` 或 `ps #<tid>` 消歧。

---

## 3. 歧义级问题（P1）

| 编号 | 位置 | 问题 | 决议 |
|---|---|---|---|
| P1-01 | 1.2 | "一律忽略大小写"会误伤字符串字面量、文件名、echo 输出文本 | **R-14**：仅命令/关键字/标识符/设备类型/单位/布尔/标签忽略；字符串内容与注释原样；文件名先精确后回退折叠查找并 WARN |
| P1-02 | 5.4 / 8.3 / 4.3 / 9.5 | 1001 既是"除零"又是"参数错误"；1002 复用为 job not loaded；1008 复用为粘贴超限 | **R-09**：保留 1001–1010 原义，新增 1011–1020 细分码，附完整映射表 |
| P1-03 | 3.2 / 11 / 12.2-2 | `ps` 一处 8 列含 CPU%、一处 7 列无 CPU%；无统计支持时是删列还是填 `-` 未定 | **R-17**：固定 8 列，无支持时 CPU% 填 `-`，不删列 |
| P1-04 | 2.2 / 3.2 | `state` 示例 "RTOS Tasks: 5" 与同节 `ps` 示例的 6 行任务不符 | 修正示例数字，两处同源 |
| P1-05 | 3.2 / 3.5 | `ps job` / `ps sys` 与名为 `job` / `sys` 的任务冲突 | 新增 `ps name <name>` / `ps #<tid>` / `ps all`，保留选项优先 |
| P1-06 | 2.1 / 4.1 / 6 / 11 | `wait <ms>`、`wait task`、`waitjob`、脚本 `WAIT TIME` / `WAIT STEP` 功能重叠，超时语法两种写法 | **R-18**：三层分开、超时统一 `timeout <t>` / `TIMEOUT <t>`，默认值由配置给定 |
| P1-07 | 2.2 / 3.2 / 11 | JOB 变量表"不与 Shell 共享"，但 `ps FEED` 显示 `$SPEED=1000` 与 Shell 同名同值 | **R-07**：默认**不继承**，需 `run <file> with $SPEED` 或 `load ... inherit` 显式注入快照 |
| P1-08 | 2.2 | `echo` 做变量替换、`print` 求值表达式，`echo 1 + 2` 结果未定 | `echo` 只做 `$VAR` 替换、不求值，原样输出其余文本；求值时用 `print` |
| P1-09 | 5.1–5.4 | int/float/bool/string 混合运算、`==` 跨类型、整数除法、短路求值、`TRUE/ON/1` 归一化、取余类型全无规则 | **R-08**：补类型提升与转换表，禁止隐式 string↔number，`AND`/`OR` 明确短路 |
| P1-10 | 多处示例 | `2s` `100ms` `100rpm` `10000Hz` `50%` `30s` 带单位字面量无语法定义 | **R-08**：新增带量纲数值字面量章节与单位表，裸数值按语句默认单位 |
| P1-11 | 1.3 / 5.2 | `//` 注释与除法 `/`、`;` 与字符串内分号的边界；去注释时机 | **R-08**：注释仅在字符串外的 token 边界识别，词法阶段剥离，字符串内不生效 |
| P1-12 | 2.2 | `run feed.ms lint` 的 lint 输出、是否执行、与块静态检查的关系未定 | lint = 解析期静态检查（块配对、跳转目标、标签唯一、嵌套深度、BREAK 位置），失败则不创建 Task，输出 `PARSE L<n>: ...` |
| P1-13 | 7.5 | `SHELL STATE TO $S` 返回值类型/格式未定 | 定义为只读字符串，格式与 `state` 单行摘要一致，便于脚本解析 |
| P1-14 | 1.4 / 9.4 / 6.6 | "块闭合后提交"提交给谁；交互态直接敲 `JOB..ENDJOB` 是否等价 run；块内 `SHELL PRINT` 怎么处理 | **R-13**：给出块栈算法；JOB 块闭合且处于 `SHELL_CTX` → 等价一次前台 `run`（可加 `&`）；块内统一 `VM_CTX` |
| P1-15 | 1.4 / 7.1 | 基础命令表与扩展命令表的查表顺序、大小写比较、与脚本关键字同名冲突未定 | **R-16**：基础 > 扩展 > 脚本；扩展不得覆盖基础（注册期拒绝），覆盖脚本关键字需启动 WARN；统一 `strcasecmp` |
| P1-16 | 7.2 / 7.6 | 结构体有 `flags` / `CMD_DANGER`，宏却不接受 flags，无任何设置途径；链接脚本符号、返回值约定、`CMD_HIDDEN` 行为均缺 | **R-16**：补 `_EX` / `_DANGER` / `_HIDDEN` 宏、链接脚本片段、返回值与错误码映射、注册期校验 |
| P1-17 | 8.1 / 7.5 / 11 | 多 JOB 并发调用 `shell_printf`，`[JOB X]` 前缀与正文可能交错 | **R-15**：输出互斥 + 单次调用原子成行，提供 `shell_printf_job()` 封装 |
| P1-18 | 6.1 / 6.4 / 9.4 | `END` 只结束 IF，但块配对算法、嵌套 8 层计数对象、`ELSE` 校验、BREAK/CONTINUE 检查时机未定 | **R-13**：块栈 + 解析期静态检查，`ELSE` 要求栈顶为 IF，BREAK 必须在循环栈内 |
| P1-19 | 2.2 / 7.6 | `reboot confirm` 与 `CMD_DANGER` 框架是两套机制 | **R-05**：`reboot` 纳入统一危险命令框架，基础命令也可带 flags |
| P1-20 | 2.2 / 7.1 | `help` 自动收录扩展命令但无分组、`CMD_HIDDEN` 规则；topic 集合不完整 | **R-16**：结构体增 `group` 字段，`help` 分组输出；补齐 topic 表 |
| P1-21 | 2.2 / 2.2 wait | `wait task` 可等待任意系统任务，但 idle/timer/shell 永不退出 | 禁止等待 `shell` / `idle`；等待其他长生命周期任务必须带 `timeout`，否则 `WARN` |
| P1-22 | 9.2 / 2.2 | Tab 补全含"文件"，但全文未说明是否存在文件系统 | **R-20**：明确 FS 可选；无 FS 时 `run <file>` 报 `ERR 1002`，仅支持粘贴缓冲区运行，Tab 不补全文件 |

---

## 4. 缺失级问题（P2）

| 编号 | 位置 | 缺失内容 | 补入章节 |
|---|---|---|---|
| P2-01 | 6.6 / 8.3 | `ON ERROR` / `ENDERROR` 无语法、无触发条件、无 `$ERR*` 变量、无重入保护 | 规格 §6.7 |
| P2-02 | 11 | cleanup / `ON STOP` 阶段（示例已有行为，规格没有） | 规格 §4.4 / §6.7 |
| P2-03 | 10.5 / 4.2 | `LABEL` / `GOTO` / `CALL` / `RETURN` 无语法；`CallStk` 已出现在详情输出 | 规格 §6.8 |
| P2-04 | 1.1 / 11 | 设备语句语法与动作表 | 规格 §12.2 |
| P2-05 | 4.4 | 完整 JOB 状态机与转移表 | 规格 §4.3 |
| P2-06 | 3.3 | 安全链路：急停、限位 1007、JOB 崩溃后电机默认状态、watchdog、WAIT 不得忙等 | 规格 §13 |
| P2-07 | 全文 | 启动流程、配置常量来源、上电自启脚本 | 规格 §14 |
| P2-08 | 9.1 | `(dbg)>` 提示符无进入方式与调试命令 | 规格 §15（标为 V1.2 可选，V1.1 仅定义提示符与入口） |
| P2-09 | 8.3 | 错误码覆盖不足 | 规格 §8.3 + 附录 A |
| P2-10 | 全文 | 一致性 / 验收清单 | 本文 §7 |
| P2-11 | 全文 | 语法 EBNF 摘要 | 规格 附录 B |

---

## 5. 12.2 待定项决策（7 / 7 关闭）

| # | 原待定项 | 结论 | 状态 |
|---|---|---|---|
| 1 | JOB Task 默认优先级与栈的配置方式 | 全局默认 + 按 JOB 名覆盖 + 单次调用覆盖，三层 | 关闭 |
| 2 | `ps` 的 CPU% 依赖 RTOS 统计 | 编译期开关；无支持时**保留列**填 `-` | 关闭 |
| 3 | 协作式 stop 能否唤醒 WAIT 阻塞中的 JOB | **必须能**，作为 V1.1 硬约束 | 关闭 |
| 4 | 任务名冲突处理 | 大小写折叠唯一，冲突拒绝并提示别名 / TID | 关闭 |
| 5 | JOB Task 退出后 TID 回收 | 延迟回收（墓碑），不立即复用 | 关闭 |
| 6 | `wait task` 超时后的部分等待语义 | 无部分等待；超时只返回错误，不改变被等待任务 | 关闭 |
| 7 | `FOR` 循环变量循环外是否可见 | 可见，保留最后值，JOB 级作用域 | 关闭 |

### 5.1 决策 1 · 优先级与栈

采用**三层覆盖**，优先级从低到高：

```
编译期全局默认  <  jobcfg <name> 注册的按名配置  <  load / run 的单次参数
```

- 全局默认：`prio = 2`（低于 `uart_rx` / `timer` 等系统任务，高于 idle），`stack = 2048 B`。
- 按名配置：`jobcfg FEED prio 3 stack 4096`（扩展命令），存于静态 profile 表。
- 单次覆盖：`run feed.ms prio 3 stack 4096` 或 `load feed.ms prio 3`。
- `ps` / `job` 详情显示**实际生效值**及来源（`global` / `profile` / `inline`）。
- 约束：栈值须在 `[SHELL_JOB_STACK_MIN, SHELL_JOB_STACK_MAX]` 内，否则 `ERR 1001`；优先级不得高于 `uart_rx`（避免饿死输入），否则 `WARN` 并钳位。

**理由**：全局默认保证零配置可跑；按名配置覆盖长期固定差异；单次参数便于现场调试，不必改代码重编译。

### 5.2 决策 2 · CPU%

- 编译期开关 `SHELL_HAS_CPU_STATS`（默认 `0`）。
- 开启时由 RTOS 运行时间统计计算滑动窗口占用；未开启时 `ps` 仍输出 8 列，CPU% 填 `-`。
- `state` 不统计 CPU 占用，避免与 `ps` 的窗口口径不一致。
- **不删列**，因为脚本或上位机按列解析 `ps` 输出，列数浮动会破坏解析。

### 5.3 决策 3 · 可中断 WAIT（硬约束）

所有 VM 等待语句（`WAIT TIME` / `WAIT STEP` / `WAIT GPIO` / `WAIT JOB`）**必须**实现为可中断的 RTOS 阻塞：

```
wait_sem + stop_flag  →  rtos_sem_take(wait_sem, timeout)
stop <name>           →  置 stop_flag 并 rtos_sem_give(wait_sem) 唤醒
被唤醒后              →  不报 1004 超时，直接进入 ON STOP 阶段
```

- 禁止忙等轮询（会饿死低优先级任务，且违背 RTOS 设计）。
- `WAIT STEP <n>` 同时监听"运动完成"与"stop"两个事件。
- 这条是 `stop` 协作式语义成立的前提：若 WAIT 不可中断，一个 `WAIT TIME 60s` 的 JOB 在 60 秒内根本停不下来。

### 5.4 决策 4 · 名称冲突

- 比较前统一折叠大小写，`FEED` 与 `feed` 视为同名。
- 冲突时**拒绝创建**（不自动改名），报 `ERR 1018`，并提示 `run <file> as <alias>` 或按 `#tid` 操作。
- 保留系统任务名不可占用（R-06）。
- `ps` 不再出现"显示冲突"这种含糊状态——V1.0 允许重名存在再让命令用 TID，会让 `kill FEED` 无法判定目标，风险过高。

### 5.5 决策 5 · TID 回收

- 采用**延迟回收 + 墓碑**：Task 删除后 TID 保留 `SHELL_TID_TOMBSTONE_MS`（默认 5000 ms）不复用，期间 `ps -a` 可见 `DELETED` 记录（含退出码、结束时间）。
- `ps` 默认不显示墓碑；`ps <tid>` 可查已删除记录，避免 `wait task` / `waitjob` 拿到 notification 后查不到信息。
- 理由：立即复用 TID 会让"延迟到达的任务退出通知"误指向新任务，是嵌入式里典型的难查 bug。

### 5.6 决策 6 · `wait task` 超时语义

- 定义为**全有或全无**：超时返回 `ERR 1004: wait task FEED timeout`，不产生"部分等待"状态，不改变被等待任务的任何状态，不自动发 stop。
- `timeout 0` 表示无限等待；未显式给出时取配置 `SHELL_WAIT_DEFAULT_MS`（默认 30000）。
- 若调用方希望超时后停止任务，需显式再发 `stop` / `kill`——把策略留给调用方，命令本身不做隐式副作用。
- `waitjob` 语义相同，仅限定等待对象为 JOB 类任务。

### 5.7 决策 7 · FOR 循环变量

- 循环变量为 **JOB 级变量**（不创建块级作用域），循环结束后可见，保留最后值（采纳 V1.0 倾向）。
- 细化边界：
  - 正常结束：值为"首次越界值"，例如 `FOR $I = 0 TO 9` 结束后 `$I = 10`。
  - `BREAK` 退出：保持当前迭代值。
  - `CONTINUE`：**先执行 `STEP` 更新**再进入下一轮判定。
  - 循环体一次未进入（如 `FOR $I = 5 TO 1`）：变量保持循环前的值。
  - 上下界与 `STEP` 在进入时求值一次，循环体内修改边界变量不影响本轮循环。

---

## 6. 相对 V1.0 的变更清单

### 6.1 破坏性变更（7 项，需同步修订 V1.0 示例）

| # | 变更 | 影响 | 迁移方式 |
|---|---|---|---|
| B-01 | `run` 默认前台，后台需 `&` | §11 的 `run mix.ms` 后立即 `> ps` 不再成立 | 改为 `run mix.ms &` |
| B-02 | `stop <name> confirm` → `stop <name> force` | 脚本/上位机发的停止指令 | 全文替换；`confirm` 遇非危险命令报 `WARN` 但继续执行 |
| B-03 | `confirm` 仅对 `CMD_DANGER` 生效 | §11 的 `kill MIX confirm`（JOB 非危险） | 改为 `kill MIX`；带 confirm 报 `WARN: confirm ignored` |
| B-04 | 错误码细分 | 依赖 1001/1002/1008 的上位机解析 | 见 §6.4；提供 `SHELL_LEGACY_ERRCODES` 兼容开关 |
| B-05 | `ps` 固定 8 列 | §11 示例的 7 列表头与数据行 | 补 CPU% 列，无统计时填 `-` |
| B-06 | VM 内 `START` / `STOP` 必须带 `JOB` | 旧脚本里的裸 `START FEED` | 改为 `START JOB FEED`；lint 会报 `PARSE` |
| B-07 | JOB 变量表默认不继承 Shell | 依赖 Shell 变量的旧脚本 | `run feed.ms with $SPEED` 或 `load feed.ms inherit` |

> V1.0 尚未实现，建议**直接以 V1.1 为准**，不引入兼容层；`SHELL_LEGACY_ERRCODES` 仅为已有上位机解析器的过渡选项。

### 6.2 新增命令（7）

| 命令 | 语法 | 说明 |
|---|---|---|
| `load` | `load <file> [as <name>] [inherit] [prio <p>] [stack <n>]` | 只加载不启动，JOB → `LOADED` |
| `unload` | `unload <name>` | 卸载非运行态 JOB，释放脚本缓冲 |
| `loads` | `loads` | 列出已加载 JOB（含 `LOADED` 与历史态） |
| `fg` | `fg <name>` | 把后台 JOB 附着为前台 |
| `ps name` / `ps #tid` / `ps all` / `ps -a` | — | 消歧 / 含墓碑 |
| `jobcfg` | `jobcfg <name> prio <p> stack <n>` | 按名覆盖 Task 参数（扩展命令） |
| `dbg` | `dbg <job>` | 进入调试模式（V1.2 可选，V1.1 保留入口） |

### 6.3 新增脚本关键字（8）

`CLAIM` / `RELEASE`（设备显式占用与释放）、`ON STOP` / `ENDSTOP`（停止钩子）、`START JOB` / `STOP JOB`（VM 内 JOB 操作）、`WAIT GPIO <n> <level>`（电平等待）、`WAIT JOB <name>`（JOB 间等待，需环形依赖检测）。

新增只读内建变量：`$ERRCODE` `$ERRLINE` `$ERRMSG` `$JOBNAME` `$TID` `$PC`。

### 6.4 新增错误码

| 码 | 含义 | 承接自 V1.0 的滥用 |
|---|---|---|
| 1011 | 除零（含 `%` 零） | 1001 |
| 1012 | 词法 / 语法错误（`PARSE` 同源码） | — |
| 1013 | 变量只读或重复定义 | — |
| 1014 | 设备忙 / 已被其他 JOB 独占 | 1002 |
| 1015 | 设备未申请 / 未占用 | 1003 |
| 1016 | 需要危险确认 | — |
| 1017 | 资源耗尽（粘贴缓冲、块深、调用栈、JOB 数） | 1008 |
| 1018 | 名称冲突 / 保留名 | 1002 |
| 1019 | 当前状态不允许该操作（如 start 未 load） | 1002 |
| 1020 | JOB 环形等待 / 非法跳转 | — |

原 1001–1010 语义保持不变，仅收窄使用范围；`1004` 统一为所有超时；`1007` 保留给限位触发。

### 6.5 新增配置项（编译期常量）

| 配置 | 默认值 | 用途 |
|---|---|---|
| `SHELL_LINE_MAX` | 256 | 行缓冲 |
| `SHELL_PASTE_MAX` | 8192 | 粘贴缓冲 |
| `SHELL_BLOCK_DEPTH` | 8 | 块嵌套 / 调用栈上限 |
| `SHELL_JOB_MAX` | 8 | 并发 JOB 上限 |
| `SHELL_JOB_PRIO` | 2 | 默认优先级 |
| `SHELL_JOB_STACK` | 2048 | 默认栈（字节） |
| `SHELL_WAIT_DEFAULT_MS` | 30000 | 默认等待超时 |
| `SHELL_TID_TOMBSTONE_MS` | 5000 | TID 墓碑保留 |
| `SHELL_HAS_CPU_STATS` | 0 | CPU% 统计开关 |
| `SHELL_HAS_FS` | 1 | 文件系统支持 |
| `SHELL_SAFE_STOP_DEC_MS` | 500 | kill / 异常时 STEP 减速时间 |
| `SHELL_LEGACY_ERRCODES` | 0 | 错误码兼容开关 |

---

## 7. 验收用例

下列用例可直接转成自动化测试（输入 → 期望输出的断言）。覆盖每一条 P0 与关键决议。

| # | 输入 | 期望输出 / 行为 | 覆盖 |
|---|---|---|---|
| T-01 | `PS` / `ps` / `Ps` | 三者输出一致 | 1.2 大小写 |
| T-02 | `set $NAME = "FeEd"` + `echo $NAME` | 输出 `FeEd`（字符串内容不被折叠） | R-14 |
| T-03 | `echo 1 + 2` | 输出 `1 + 2`（echo 不求值） | P1-08 |
| T-04 | `print 1 + 2 * 3` | `7` | §5 |
| T-05 | `print 7 / 2` | `3`（整数除法截断） | R-08 |
| T-06 | `print 7 % 0` | `ERR 1011: modulo by zero` | R-09 |
| T-07 | `print $UNDEF + 1` | `ERR 1009: undefined variable` | §5.4 |
| T-08 | `print "A" + 1` | `ERR 1010: type mismatch` | R-08 |
| T-09 | 交互态直接输入 `WAIT TIME 100ms` | `ERR 1001: invalid argument for wait`（被 Shell 命令截获） | R-01 |
| T-10 | JOB 内 `WAIT TIME 100ms` | 正常等待 100 ms | R-01 |
| T-11 | JOB 内 `SHELL KILL FEED` | 拒绝，报 `ERR 1016` / PARSE（不在白名单） | R-01 |
| T-12 | JOB 内 `START FEED` | `PARSE L<n>: START requires JOB keyword` | B-06 |
| T-13 | `start FEED`（未 load） | `ERR 1019: job FEED is not LOADED` | R-02 / R-09 |
| T-14 | `load feed.ms as FEED` + `jobs` | 显示 `FEED LOADED` | R-02 |
| T-15 | `run feed.ms` 后立即输入 `ps`（脚本含 5 s WAIT） | Shell 阻塞，直到 JOB 结束才回到 `>` | R-04 |
| T-16 | `run feed.ms &` 后立即 `ps` | 立即回到 `>`，`ps` 可见 RUNNING 的 JOB Task | R-04 |
| T-17 | 前台 JOB 运行中按 Ctrl-C | 触发 `stop`，执行 `ON STOP`，`JOB → ABORTED`，设备已安全收尾 | R-03 / R-04 |
| T-18 | 后台 JOB 运行中按 Ctrl-C | 只清空当前行，后台 JOB 不受影响 | R-04 |
| T-19 | `WAIT TIME 60s` 的 JOB 执行 `stop FEED` | 1 s 内退出（WAIT 被唤醒），不报 1004 | 决策 3 |
| T-20 | `kill MIX`（JOB 任务） | 无 confirm 即成功；不执行钩子；`STEP` 执行减速停后释放 | R-03 / R-05 |
| T-21 | `kill timer` | `ERR 1016: dangerous command requires confirm` | R-05 |
| T-22 | `kill timer confirm` | 成功，带 `WARN: system task deleted` | R-05 |
| T-23 | `kill FEED confirm`（非危险） | 成功 + `WARN: confirm ignored` | B-03 |
| T-24 | `kill shell` / `kill idle` / `kill shell confirm` | 全部拒绝 | R-05 |
| T-25 | `run feed.ms` 两次（无 `as`） | 第二次 `ERR 1018: job FEED already running` | R-06 |
| T-26 | `run feed.ms as feed` | 大小写折叠后与 `FEED` 冲突，同上 | R-06 |
| T-27 | `run feed.ms as idle` | `ERR 1018: reserved name` | R-06 |
| T-28 | 无 `SHELL_HAS_CPU_STATS` 时 `ps` | 表头仍为 8 列，CPU% 列全为 `-` | 决策 2 |
| T-29 | 两个 JOB 同时 `SHELL PRINT` 各 100 行 | 无交错行，每行前缀与正文完整 | R-15 |
| T-30 | JOB A 占用 `STEP0` 时 JOB B 执行 `STEP 0 START` | `ERR 1014: STEP0 busy (owner: JOB A)` | R-10 |
| T-31 | 两个 JOB 同时 `ADC 0 READ` | 均成功（ADC 读为 SHARED） | R-10 |
| T-32 | `kill` 一个正在运动的 JOB | STEP 执行 `SHELL_SAFE_STOP_DEC_MS` 减速停，PWM 关断，设备释放 | R-10 / R-19 |
| T-33 | `load x.ms prio 9 stack 128` | `WARN` 钳位 + `ERR 1001`（栈越界） | 决策 1 |
| T-34 | `FOR $I = 0 TO 9 ... ENDFOR` + `SHELL PRINT $I` | 输出 `10` | 决策 7 |
| T-35 | `FOR $I = 5 TO 1` 后打印 `$I` | 循环前的值，循环体未执行 | 决策 7 |
| T-36 | 交互输入 `IF 1` / `SHELL PRINT "x"` / `ELSE` / `SHELL PRINT "y"` / `END` | 输出 `[JOB ...] x`，不输出 y | R-13 |
| T-37 | 续行中单独输入 `.` | 丢弃缓冲，回到 `>`，无 Task 创建 | §9.4 |
| T-38 | 粘贴超过 8 KB | `ERR 1017: paste buffer overflow` | R-09 |
| T-39 | JOB A `WAIT JOB B` 且 B `WAIT JOB A` | `ERR 1020: circular job wait`（解析期或运行期检测） | §6.8 |
| T-40 | `state` 与 `ps` 的任务计数 | 两者任务总数一致 | P1-04 |

---

## 8. 实施建议与范围边界

**建议实现顺序**（每一步都可独立验证）：

1. 词法 + 双上下文分派（R-01）、大小写边界（R-14）、注释剥离（R-08）—— 没有这一步后面全部会返工。
2. 表达式求值 + 单位字面量 + 错误码框架。
3. RTOS 适配层：任务枚举、TID 墓碑、`ps` / `kill` / `pause` / `resume`、危险确认框架。
4. 设备注册表与设备语句（含安全收尾），先在无 JOB 的单任务环境下验证。
5. JOB 生命周期：load → Task 创建 → VM → ON ERROR / ON STOP → cleanup → Task 自删除。
6. Shell 前台/后台、Ctrl-C、粘贴、续行块、历史与补全。
7. 扩展命令注册段与链接脚本。
8. 安全链路联调（急停 / 限位 / watchdog）。

**明确不在 V1.1 范围内**（避免过度设计）：

- 调试器的完整断点/单步命令集（V1.2，V1.1 只定义提示符与入口）。
- JOB 间变量共享、消息传递、事件订阅。
- `RESUME ERROR`（跳过出错语句继续执行）。
- 浮点双精度、数组、结构体、自定义函数带参数。
- 脚本级设备共享写权限协商。

**需要你确认的 3 个产品决策**（技术上均可实现，但取决于产品定位）：

1. `run` 默认前台是否符合现场操作习惯？如果上位机批量下发脚本，默认后台可能更顺手（可改成配置开关 `SHELL_RUN_FOREGROUND`）。
2. 变量表默认不继承（R-07）是否会影响现有脚本资产？若有大量脚本依赖 Shell 变量，可改为默认继承快照。
3. 是否存在文件系统？若目标板只有 Flash 上的脚本槽位，`run <file>` 应改为 `run <slot>`，Tab 补全与 `load` 的语义要跟着调整。

---

*本文为 V1.0 的增量评审记录；完整、自洽、可直接实现的规格见 `IoT-Shell-V1.1-规格说明.md`。*
