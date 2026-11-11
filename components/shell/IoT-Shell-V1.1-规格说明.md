# IoT Shell 规格说明 V1.1

> **IoT Shell** 是运行于 RTOS 之上的交互式命令行环境，用于管理 RTOS 任务、执行 MotionScript 脚本、调用设备驱动与调试系统。
> Shell 命令、脚本语句、扩展命令在统一输入端共存，**标识符一律忽略大小写**。
> 本版相对 V1.0 的修订依据见 `IoT-Shell-V1.1-需求完善与决议.md`。

| 项 | 内容 |
|---|---|
| 版本 | V1.1 (Draft for Review) |
| 日期 | 2026-11-10 |
| 执行模型 | 每个 JOB 一个独立 RTOS Task（非协程） |
| 解析模型 | `SHELL_CTX` / `VM_CTX` 双上下文分派 |

---

# 1. 总则

## 1.1 三层模型

| 层 | 对象 | 管理命令 | 说明 |
|---|---|---|---|
| **Task 层** | RTOS 任务 | `ps` `kill` `pause` `resume` | Shell 直接管理内核任务 |
| **JOB 层** | 脚本任务 | `jobs` `job` `load` `run` `start` `stop` `waitjob` | 每个 JOB **独立启动一个 RTOS Task** |
| **设备层** | STEP/PWM/GPIO/ADC… | 脚本语句 | 由 JOB 通过设备注册表访问 |

```
Shell Task (1 个)
  ├─ 管理 → 所有 RTOS Task（ps/kill/pause/resume）
  │           ├─ 系统任务（idle、timer、驱动线程…）
  │           └─ JOB Task（每个 JOB 一个）
  ├─ 启动 → JOB Task（run/start）
  └─ 附着 → 最多 1 个「前台 JOB」（见 §4.6）
```

**关键关系：**

- `ps` 列出**所有** RTOS 任务，含系统任务与 JOB 任务；`jobs` / `job` 是 `ps` 的 JOB 视图，附带脚本 PC、占用设备等脚本层信息。
- `kill` / `pause` / `resume` 操作 RTOS 任务，按任务名或 TID；`start` / `stop` 操作 JOB 生命周期。
- 每个 JOB 启动时创建一个独立 RTOS Task；JOB 结束时 Task 自删除或被回收，设备由内核统一释放。
- 「前台 / 后台」只是 Shell Task 的注意力归属，**不改变**上述调度模型。

## 1.2 大小写规则

**忽略大小写的范围**：Shell 命令名、扩展命令名、脚本关键字、JOB 名 / Task 名、变量名、设备类型名、单位后缀、LABEL 名、布尔字面量、选项名（`job` `sys` `all` `name` `confirm` `force` `timeout`）。

```
ps / PS / Ps                    等价
STEP 0 START / step 0 start      等价
kill FEED / KILL feed            等价
set $SPEED = 1 / SET $speed = 1  同一变量
```

**不忽略大小写的范围**（原样保留）：

| 对象 | 说明 | 示例 |
|---|---|---|
| 字符串字面量 | 内容按原样存储与输出 | `set $NAME = "FeEd"` → `echo $NAME` 输出 `FeEd` |
| `echo` 的其余文本 | 只做 `$VAR` 替换，不做折叠 | `echo Hello World` 原样输出 |
| 注释文本 | 不解析 | `; TODO: Fix Me` |
| 文件名 | 见下方查找规则 | `run Feed.ms` |

**文件名查找规则**：

1. 若 `SHELL_HAS_FS = 0`，`run` / `load` 一律报 `ERR 1002: file system not available`，只能用粘贴缓冲区运行。
2. 若文件系统大小写敏感：先按原样查找；失败后做一次折叠查找，命中则输出 `WARN: matched 'feed.ms' for 'Feed.ms'` 并继续执行；仍未命中报 `ERR 1002`。
3. 若文件系统大小写不敏感：直接查找。

**内部表示**：所有参与比较的标识符统一折叠为大写；`ps` / `jobs` 输出时回显创建时的原始拼写（便于人工核对），比较与查找一律使用折叠值。

## 1.3 行格式与词法

```
COMMAND [ARG]... [; COMMENT]
COMMAND [ARG]... [// COMMENT]
```

- `;` 或 `//` 之后为注释，**仅在字符串字面量之外、且处于 token 边界时**识别。字符串内的 `;`、`//`、`/` 不触发注释。
- `/` 单独出现是除法；`//` 才启动注释。词法阶段先剥离注释，再交给解析器。
- 空行 / 纯注释行忽略。
- 行最大 `SHELL_LINE_MAX`（默认 256）字节；超长报 `ERR 1017: line too long`。
- 粘贴缓冲上限 `SHELL_PASTE_MAX`（默认 8192）字节；超出报 `ERR 1017: paste buffer overflow`。
- token 分隔：空白（空格 / Tab）；字符串用 `"` 包裹，支持 `\"` 转义；参数分隔可用 `,`（设备语句的参数列表中）。

## 1.4 输入判定顺序（V1.1 修订）

```
1) 词法：剥离注释（字符串外）、去首尾空白
2) 空行 / 纯注释 → 忽略
3) 确定解析上下文 ctx（见 §1.5）
4) 若 ctx = VM_CTX 且块栈非空 → 按脚本语句处理（不做命令查表）
5) 取第一 token（折叠大小写）
6) 按 ctx 查表：
     SHELL_CTX : 基础命令 → 扩展命令 → 脚本语句（转交互 JOB）
     VM_CTX    : 脚本语句 → SHELL <sub> 内建 → 未知
7) 命中命令 → 执行；命中脚本语句 → 投递给当前 VM 或创建交互 JOB
8) 无法识别 → ERR 1012 / unknown command
```

> **与 V1.0 的关键差异**：V1.0 第 3 步把"续行态"当作一种特殊分支，但第 4~5 步仍只有一张命令表。V1.1 把上下文提升为第一步判定，见 §1.5。

## 1.5 双上下文分派

| 上下文 | 何时成立 | 说明 |
|---|---|---|
| `SHELL_CTX` | 主提示符 `>` 下的输入行、粘贴态的每个顶层行、且块栈为空 | 命令优先 |
| `VM_CTX` | JOB Task 内执行的脚本行、续行块内（块栈非空）的任何行 | 脚本语句优先 |

**同名词义由上下文唯一确定，不做参数猜测**：

| token | `SHELL_CTX` | `VM_CTX` |
|---|---|---|
| `WAIT` | Shell 阻塞等待：`wait <time>` / `wait task <name>` | 脚本等待：`WAIT TIME <t>` / `WAIT STEP <n>` / `WAIT GPIO <n> <level>` / `WAIT JOB <name>` |
| `START` | `start <name>` 启动已加载 JOB | **必须**写 `START JOB <name>`，裸 `START <name>` 报 `PARSE` |
| `STOP` | `stop <name> [force]` | **必须**写 `STOP JOB <name>` |
| `SET` / `UNSET` | Shell 变量表 | JOB 变量表 |
| `PRINT` | 对 Shell 变量表求值打印 | 脚本内无 `PRINT`，用 `SHELL PRINT` |

**脚本内调用 Shell 能力**统一使用 `SHELL <sub>`（§6.10）。`SHELL <cmd>` 透传仅限白名单：无副作用、可重入、不操作 JOB 生命周期的命令（默认 `ver` `mem` `ps` `state` `help`）。透传 `kill` / `reboot` / `stop` / `run` / `load` 一律拒绝并报 `ERR 1016`。

## 1.6 全局常量

见 §14.3 配置常量表。所有上限类错误统一用 `ERR 1017`。

---

# 2. 基础命令

## 2.1 命令一览

| 命令 | 语法 | 说明 |
|---|---|---|
| `help` | `help [topic]` | 帮助 |
| `ver` | `ver` | 版本 |
| `echo` | `echo <text>` | 回显（仅 `$VAR` 替换） |
| `reboot` | `reboot [confirm]` | 软复位（危险命令） |
| `print` | `print <expr>` | 对表达式求值并打印 |
| `set` | `set $VAR = <expr>` | 设 Shell 变量 |
| `unset` | `unset $VAR` | 删 Shell 变量 |
| `wait` | `wait <time>` / `wait task <name\|#tid> [timeout <t>]` | 阻塞等待 |
| `load` | `load <file> [as <name>] [inherit] [prio <p>] [stack <n>]` | 加载脚本，JOB → `LOADED` |
| `unload` | `unload <name>` | 卸载非运行态 JOB |
| `loads` | `loads` | 列出已加载 JOB |
| `run` | `run [<file>] [&] [as <name>] [with $V...] [prio <p>] [stack <n>] [lint]` | 加载并启动 JOB |
| `start` | `start <name>` | 启动已加载 JOB |
| `stop` | `stop <name> [force]` | 停止 JOB |
| `fg` | `fg <name>` | 把后台 JOB 附着为前台 |
| `state` | `state` | 系统状态总览 |

## 2.2 help / ver

```
help                    列出所有命令（基础 / 任务 / JOB / 设备 / 扩展，分组）
help ps                 查看 ps 用法
help script             脚本关键字速查
help expr               表达式语法
help units              带单位数值字面量
help devices            设备语句语法
help errors             错误码表
help <extcmd>           扩展命令自带的 usage / help
```

`CMD_HIDDEN` 标记的扩展命令不出现在 `help` 列表中，但 `help <name>` 仍可查询。

```
> ver
IoT Shell v1.1
Build: 2026-11-10
RTOS: <your-rtos> / <version>
VM: per-job task model
Devices: STEP(4) PWM(2) GPIO(8) ADC(4) SENSOR(2) RELAY(2)
FS: enabled | CPU stats: disabled
```

## 2.3 echo / print 边界

| 命令 | 行为 |
|---|---|
| `echo <text>` | 逐个 token 做 `$VAR` 替换（未定义变量输出原样 `$VAR` 并 `WARN`），**不求值表达式**，其余文本原样输出 |
| `print <expr>` | 整行按表达式求值后打印结果 |

```
echo hello world        → hello world
echo $SPEED             → 1000
echo "a b"              → a b
echo 1 + 2              → 1 + 2
print 1 + 2             → 3
```

## 2.4 set / unset

```
set $SPEED = 1000
set $COUNT = $COUNT + 1
set $NAME = "FEED"
set $ENABLE = TRUE

unset $SPEED
```

- 变量作用域：Shell 上下文；JOB Task 内有独立变量表（§4.5）。
- 变量名为大小写折叠；`$ERRCODE` `$ERRLINE` `$ERRMSG` `$JOBNAME` `$TID` `$PC` 为只读内建变量，赋值报 `ERR 1013`。
- 右值未定义变量 → `ERR 1009`；类型不匹配 → `ERR 1010`。

## 2.5 wait 体系

```
wait 1000                       等待 1000 ms
wait 2s                         等待 2 秒
wait 30s                        等待 30 秒
wait task FEED                  等待 RTOS Task FEED 结束
wait task #5 timeout 30s        按 TID 等待，30 s 超时
wait task FEED timeout 0        无限等待
waitjob FEED timeout 30s        等待 JOB 结束（见 §4.9）
```

**规则：**

- `wait <time>` 阻塞 Shell Task，期间 Ctrl-C 可中断（返回 `WARN: wait interrupted`，无错误码）。
- `wait task` 可等待任意**可结束**的 RTOS Task（不限 JOB）；禁止等待 `shell` / `idle`（报 `ERR 1001`）；等待其他长生命周期系统任务时若未给 `timeout` 输出 `WARN: waiting on a long-lived task`。
- 超时统一语法 `timeout <t>`；未显式给出时取 `SHELL_WAIT_DEFAULT_MS`（默认 30 s）；`timeout 0` 表示无限。
- 超时 → `ERR 1004: wait task FEED timeout`，**不改变被等待任务的任何状态，不自动发 stop**，无"部分等待"语义。
- VM 内的等待语句另见 §12.4，与本节不共用参数语法。

## 2.6 load / unload / loads

```
load feed.ms                      JOB 名从文件或首行 JOB <name> 推断
load feed.ms as FEED2             显式别名（避免名称冲突）
load feed.ms inherit              变量表继承 Shell 快照
load feed.ms prio 3 stack 4096    单次覆盖 Task 参数
loads
unload FEED
```

- `load` 只做解析 + 静态检查（lint 子集），**不创建 RTOS Task**，JOB 进入 `LOADED`。
- 脚本名推断顺序：`as <name>` → 文件首行 `JOB <name>` → 去扩展名的文件名；推断结果按 §3.5 校验。
- 解析失败：不产生 JOB 对象，输出 `PARSE L<n>: ...`。
- `unload` 仅允许 `LOADED` / `COMPLETED` / `ERROR` / `ABORTED` 状态；其他状态报 `ERR 1019`。
- `loads` 输出：`NAME STATE SCRIPT PC TASK VARS`。

## 2.7 run

```
run feed.ms                 前台：加载 + 启动，Shell 阻塞至 JOB 结束
run feed.ms &               后台：加载 + 启动，立即回到 >
run feed.ms as FEED2 &      指定别名
run feed.ms with $SPEED     变量注入（快照）
run feed.ms lint            先静态检查，通过才启动
run                         执行当前粘贴缓冲区
```

- `run <file>` ≡ `load <file>` + `start <name>`，为**原子操作**：解析失败则不创建 Task、不产生 JOB 记录。
- 每次 `run` 创建一个新的 RTOS Task 承载 JOB，分配新的 `job_id` 与 TID。
- 输出带 `[JOB <name>]` 前缀（前台同样带）。
- Ctrl-C 对**前台** JOB 发 `stop`（协作式）；后台 JOB 不受 Ctrl-C 影响。
- `run` 省略文件参数时执行当前粘贴缓冲区；执行后清空缓冲。
- `lint` 检查项见 §6.7.3；失败输出 `PARSE L<n>: ...` 且不启动。
- 与 V1.0 差异：`run` 默认前台，后台需 `&`。

## 2.8 state

```
> state
RTOS Tasks: 6 (RUNNING=2 READY=2 BLOCKED=1 SUSPENDED=1)
JOBs      : 2 (FEED=RUNNING, MIX=SUSPENDED)
Shell Vars: SPEED=1000, COUNT=3
Devices   : STEP0→FEED, PWM0→FEED, STEP1→MIX
Heap      : 12.3 KB used / 32 KB
```

- 任务计数必须与同刻 `ps` 的行数一致（不含 `DELETED` 墓碑）。
- `Shell Vars` 只列 Shell 变量表，不含 JOB 变量。
- `SHELL STATE TO $S`（§6.10）得到的字符串为上述摘要的单行压缩形式：`key=value;...`。

## 2.9 命令通用约定

- 选项顺序不敏感；`confirm` / `force` 等后缀可出现在参数列表任意位置。
- 返回值：扩展命令返回 `0` 成功，负数为错误码绝对值（§7.2）；内建命令统一走 §8.2 格式输出。
- 所有命令参数在折叠大小写后比较；字符串值保持原样。

---

# 3. 系统任务管理

## 3.1 命令一览

| 命令 | 语法 | 说明 |
|---|---|---|
| `ps` | `ps [all\|job\|sys\|name <name>\|#<tid>\|-a]` 或 `ps <name>` | 列出 / 查看 RTOS 任务 |
| `kill` | `kill <task\|#tid> [confirm]` | 删除任务 |
| `pause` | `pause <task\|#tid> [confirm]` | 挂起任务 |
| `resume` | `resume <task\|#tid> [confirm]` | 恢复任务 |

## 3.2 ps —— 查看 RTOS 任务

```
ps            列出所有活动任务（默认，等价 ps all）
ps job        只列 JOB 类任务（等价 jobs 的任务列）
ps sys        只列系统任务
ps FEED       查看某任务详情（名字）
ps name FEED  同上，显式消歧（当存在名为 job/sys 的任务时使用）
ps #5         按 TID 查看
ps -a         含 DELETED 墓碑记录
```

**列表输出固定 8 列**（`SHELL_HAS_CPU_STATS = 0` 时 CPU% 填 `-`，**不删列**）：

```
> ps
TID  NAME      TYPE   STATE      PRIO  STACK  USED  USE%   CPU%   JOB
---  --------  ----  ---------  ----  -----  ----  -----  -----  ----
  1  shell     SYS    READY         3   1024   512  50.0%   -      -
  2  idle      SYS    RUNNING       0    256    96  37.5%   -      -
  3  timer     SYS    READY         5    512   208  40.6%   -      -
  4  uart_rx   SYS    BLOCKED       4    512   184  35.9%   -      -
  5  FEED      JOB    RUNNING       2   2048   960  46.9%   -    FEED
  6  MIX       JOB    SUSPENDED     2   2048   688  33.6%   -    MIX
```

| 列 | 说明 |
|---|---|
| `TID` | 任务 ID（RTOS 分配，退出后按 §3.5 延迟回收） |
| `NAME` | 任务名（回显原始拼写） |
| `TYPE` | `SYS` 系统任务 / `JOB` 脚本任务 |
| `STATE` | READY / RUNNING / BLOCKED / SUSPENDED / DELETED（仅 `ps -a`） |
| `PRIO` | 优先级（数值越小越高，按 RTOS 约定调整） |
| `STACK` | 栈大小（字节） |
| `USED` | 已用栈字节数（栈魔数水位反推）；未启用栈检查时为 `-` |
| `USE%` | 栈使用率 |
| `CPU%` | 粗略 CPU 占用；无统计支持时为 `-` |
| `JOB` | 若为 JOB 任务，对应 JOB 名；否则 `-` |

**详情输出**：

```
> ps FEED
Name    : FEED
TID     : 5
JobID   : 3
Type    : JOB
State   : RUNNING
Prio    : 2 (source: profile)
Stack   : 2048 (used 812, peak 1024)
CPU     : 15.2%
Job     : FEED
Script  : feed.ms
PC      : L18
CallStk : FEED → HOME (depth 1)
Vars    : $I=3, $SPEED=1000
Devices : STEP0, PWM0, GPIO0
Started : 2026-11-10 12:34:56 (uptime 12.3s)
```

## 3.3 kill —— 删除任务

```
kill <task>             删除指定任务
kill #5                 按 TID 删除
kill FEED               删除 JOB FEED 对应 Task，JOB → ABORTED
kill timer              报 ERR 1016（系统任务需确认）
kill timer confirm      删除系统任务（危险）
kill shell / kill idle  硬拒绝（confirm 也无效）
```

**规则：**

| 目标 | 确认 | 脚本钩子 | JOB 状态 | 设备 |
|---|---|---|---|---|
| JOB Task | 不需要 | 不执行 `ON ERROR` / `ON STOP` | `ABORTED` | 内核执行安全收尾后释放 |
| SYS Task | 必须 `confirm` | — | — | 无 |
| `shell` / `idle` | 拒绝 | — | — | — |

- 删除 JOB 任务时**不执行任何脚本钩子**，但**必须**执行内核级设备安全收尾（§4.4 / §13），顺序为：置停止标志 → STEP 减速停 / PWM 关断 / GPIO 回安全电平 → 释放设备注册表项 → 删除 Task。
- 对非危险操作附带 `confirm` → 成功但输出 `WARN: confirm ignored`。
- 删除系统任务且未带 `confirm` → `ERR 1016: dangerous command requires confirm`。
- **`kill` 不等于硬件急停**（急停走独立安全链路，见 §13）。

## 3.4 pause / resume

```
pause FEED              RUNNING/READY/BLOCKED → SUSPENDED
resume FEED             SUSPENDED → 恢复原状态（BLOCKED 则重新评估唤醒条件）
pause timer             报 ERR 1016
pause uart_rx confirm   挂起系统任务（危险）
```

- 对 JOB 任务：JOB 状态同步为 `PAUSED`；`resume` 后回到 `RUNNING`。
- 对 `BLOCKED` 任务挂起：唤醒条件保留，`resume` 后重新评估。
- 挂起 `idle` / `shell` 自身：拒绝。
- 挂起系统任务需 `confirm`；`resume` 一个因危险操作而挂起的系统任务不需要确认（恢复不引入风险）。
- 挂起**前台 JOB** 时 Shell 仍阻塞等待；`resume` 或 `stop` 后才回到 `>`。

## 3.5 任务名 vs TID / 命名唯一性

- **内部主键**：JOB 使用单调递增、永不复用的 `job_id`；Task 使用 RTOS 分配的 TID。名称仅为显示与查找别名。
- **唯一性**：所有任务名按大小写折叠后唯一。`FEED` ≡ `feed`。冲突时**拒绝创建**，报 `ERR 1018: job FEED already running`，并提示 `run <file> as <alias>` 或按 `#tid` 操作。
- **保留名**：`shell` / `idle` / `timer` / `uart_rx` 及 RTOS 实际系统任务名不可作为 JOB 名（`ERR 1018: reserved name`）。
- **保留选项**：`ps` 的 `all` / `job` / `sys` / `name` / `-a` 优先解析为选项；若任务确实同名，用 `ps name <name>` 或 `ps #<tid>`。
- **参数匹配顺序**：先按名字折叠匹配，失败后按 `#tid` 解析；裸数字优先按 TID 处理。
- **TID 回收**：延迟回收。Task 删除后保留 `SHELL_TID_TOMBSTONE_MS`（默认 5000 ms）墓碑，期间 TID 不复用；`ps -a` 可见 `DELETED` 记录（含退出码、结束时间），`ps #<tid>` 可查。理由：避免延迟到达的任务退出通知误指向新任务。

## 3.6 危险命令框架

`CMD_DANGER` 是命令属性（基础命令与扩展命令均可带），执行前统一要求 `confirm`：

```
kill timer confirm
pause uart_rx confirm
reboot confirm
```

| 情况 | 行为 |
|---|---|
| 危险命令无 `confirm` | `ERR 1016: dangerous command requires confirm` |
| 危险命令带 `confirm` | 执行，必要时输出 `WARN` |
| 非危险命令带 `confirm` | 执行 + `WARN: confirm ignored` |
| `kill shell` / `kill idle` / `pause shell` / `pause idle` | 硬拒绝，`confirm` 无效 |

`confirm` 的语义在全文唯一：**"我确认绕过保护"**。强制停止 JOB 使用 `stop <name> force`，不使用 `confirm`。

---

# 4. JOB 管理（脚本任务视图）

> JOB 是脚本执行单元，**每个 JOB 承载于一个独立 RTOS Task**。`jobs` / `job` 是 `ps` 的脚本视图；`load` / `run` / `start` / `stop` 管理其生命周期。

## 4.1 命令一览

| 命令 | 语法 | 说明 |
|---|---|---|
| `load` | `load <file> [as <name>] [inherit] [prio <p>] [stack <n>]` | 加载，JOB → `LOADED` |
| `unload` | `unload <name>` | 卸载非运行态 JOB |
| `loads` | `loads` | 列出已加载 JOB |
| `run` | `run <file> [&] [as <name>] [with $V...] [lint]` | 加载并启动（原子） |
| `jobs` | `jobs` | 列出所有活动 JOB |
| `job` | `job <name>` | JOB 详情 |
| `start` | `start <name>` | 启动已加载 JOB |
| `stop` | `stop <name> [force]` | 协作式 / 强制停止 |
| `fg` | `fg <name>` | 附着为前台 |
| `waitjob` | `waitjob <name> [timeout <t>]` | 阻塞等待 JOB 结束 |

## 4.2 完整状态机

```
                 load / run(parse ok)
   (none) ─────────────────────────────► LOADED
                                           │ start / run
                                           ▼
        ┌──────────────────────────► RUNNING ◄────────┐
        │                              │  ▲           │ resume
        │ pause                        │  │           │
        ▼                              │  │           │
     PAUSED ──────────────────────────┘  │           │
        │                                 │           │
        │ resume                          │           │
        └─────────────────────────────────┘           │
                                           │           │
              ┌──────────────────────────┼───────────┤
              │ ENDJOB        error      │ stop      │ kill / stop force
              ▼              ▼           ▼           ▼
          COMPLETED        ERROR      ABORTED     ABORTED
              │              │           │           │
              └──────────────┴───────────┴───────────┘
                             │ Task 自删除 / 被删除 + cleanup
                             ▼
                     (可 start 重启新实例 / unload)
```

| 状态 | 含义 | 允许的操作 |
|---|---|---|
| `LOADED` | 已解析、未创建 Task | `start` `unload` `job` `loads` |
| `RUNNING` | Task 已创建并执行 | `pause` `stop` `kill` `fg` `waitjob` `job` |
| `PAUSED` | Task 被挂起 | `resume` `stop` `kill` `job` |
| `STOPPING` | 已置停止标志，等待指令边界退出（瞬态） | 仅内部 |
| `COMPLETED` | 脚本 `ENDJOB` 正常结束 | `start`（重启）`unload` `job` |
| `ERROR` | 运行时错误并执行完 `ON ERROR` | `start` `unload` `job` |
| `ABORTED` | 被 `stop` / `kill` / Ctrl-C 终止 | `start` `unload` `job` |

- 对已结束状态的 JOB 执行 `start` = **全新实例**：重新创建 Task、分配新 TID、PC 复位、变量表按 §4.5 重新初始化，但保留 `job_id` 与脚本缓冲。
- `STOPPING` 不出现在 `jobs` 列表中（合并显示为 `RUNNING`），但 `job <name>` 详情可见。

## 4.3 生命周期与清理顺序

| 结束方式 | 脚本钩子 | 结束状态 | 内核 cleanup |
|---|---|---|---|
| `ENDJOB` | 可选 `ON STOP`（缺省无） | `COMPLETED` | 执行 |
| 运行时错误 | `ON ERROR`（置 `$ERR*`） | `ERROR` | 执行 |
| `stop <name>`（协作式） | `ON STOP` | `ABORTED` | 执行 |
| `stop <name> force` | `ON STOP`；未定义时退化为 `ON ERROR` | `ABORTED` | 执行 |
| `kill <task>` | **不执行任何钩子** | `ABORTED` | **仍执行** |
| Ctrl-C（前台） | 同 `stop` | `ABORTED` | 执行 |

**cleanup 是内核阶段，不依赖脚本**：

```
1) 置 stop_flag，唤醒所有可中断 WAIT        （§12.4 / 决策 3）
2) 执行脚本钩子（ON ERROR / ON STOP），若允许；钩子内出错不再递归触发
3) 设备安全收尾：STEP 减速停 → PWM 关断 → GPIO 回安全电平 → RELAY 断开
4) 释放设备注册表全部 owner = 本 job_id 的条目
5) 打印结束状态，释放 JOB 变量表
6) Task 自删除 / 被删除；登记 TID 墓碑
7) 向等待者（waitjob / wait task / 前台 Shell）投递退出通知
```

- 步骤 3~4 **必须**执行，即使 JOB 是被 `kill` 强杀、或脚本钩子自身崩溃。
- 步骤 3 的参数由 `SHELL_SAFE_STOP_DEC_MS` 与 §13 的设备安全状态表决定。
- Shell 通过任务退出通知（RTOS join / 信号量）感知 JOB 结束，不轮询。

## 4.4 start / stop / fg

```
start FEED              启动 LOADED（或已结束）的 JOB，创建 Task
stop FEED               协作式：置停止标志，在下一个指令边界退出
stop FEED force         强制：立即进入 cleanup，保留 ON ERROR 兼容行为
fg FEED                 把后台 JOB 附着为前台（Shell 阻塞）
```

- `start` 要求 JOB 处于 `LOADED` / `COMPLETED` / `ERROR` / `ABORTED`；未加载报 `ERR 1019: job FEED is not LOADED`；`RUNNING` / `PAUSED` 报 `ERR 1019: job FEED is RUNNING`。
- `stop` 协作式：JOB 在下一个**指令边界**（每条脚本语句执行完、每个可中断 WAIT 被唤醒后）退出，可在 `ON STOP` 中做有序收尾。
- `stop` 与 `kill` 的区别：`stop` 走协作路径并执行钩子，`kill` 强删 Task 且不执行钩子；两者**都**执行设备安全收尾。
- `fg` 对前台 JOB 无效（已是前台）；对 `PAUSED` JOB 附着后仍阻塞，需 `resume`。

## 4.5 JOB 变量表

- 每个 JOB 有独立变量表，**默认不与 Shell 共享、不继承**。
- 三种初始化方式：

| 方式 | 语法 | 语义 |
|---|---|---|
| 空白（默认） | `run feed.ms` | 变量表为空，仅含只读内建变量 |
| 全量快照 | `load feed.ms inherit` | 启动时复制 Shell 变量表（值拷贝，之后互不影响） |
| 显式注入 | `run feed.ms with $SPEED $COUNT` | 只注入指定变量的快照 |

- JOB 结束时变量表销毁；`job <name>` 在 `COMPLETED` 等历史态仍可查看最后快照，直到 `unload`。
- 脚本内 `SET` / `UNSET` 只作用于本 JOB 变量表，不会写回 Shell。
- 只读内建变量：`$ERRCODE` `$ERRLINE` `$ERRMSG` `$JOBNAME` `$TID` `$PC`；赋值报 `ERR 1013`。

## 4.6 前台 / 后台

| 形式 | Shell Task | Ctrl-C | 说明 |
|---|---|---|---|
| `run feed.ms` | 阻塞，等待该 JOB 结束 | 发 `stop` 给该 JOB | 默认前台 |
| `run feed.ms &` | 立即回到 `>` | 清空当前行 | 后台 |
| `start FEED` | 立即回到 `>` | 清空当前行 | 后台 |
| `fg FEED` | 阻塞并附着到指定后台 JOB | 发 `stop` | 前台切换 |

- **前台 JOB** = 当前附着到 Shell Task 的那一个 JOB，全局最多一个；它只是 Shell 的注意力归属，不改变"每个 JOB 独立 RTOS Task"的调度模型。
- 前台 JOB 结束时 Shell 打印 `JOB FEED → COMPLETED, Task deleted` 并回到 `>`。
- 多个后台 JOB 并发时，Ctrl-C **不影响**任何后台 JOB；停止需显式 `stop <name>` / `kill <task>`。

## 4.7 jobs / job

```
> jobs
NAME   TASK   TID  STATE      PC    DEVICES
FEED   FEED    5   RUNNING    L18   STEP0 PWM0 GPIO0
MIX    MIX     6   SUSPENDED  L08   STEP1

> job FEED
Name    : FEED
Task    : FEED (TID 5, JobID 3)
State   : RUNNING
Prio    : 2 (source: profile)   Stack: 2048
Script  : feed.ms
PC      : L18
CallStk : FEED → HOME (depth 1)
Vars    : $I=3, $SPEED=1000, $TARGET_WEIGHT=1000
Devices : STEP0, PWM0, GPIO0
```

- `jobs` 只列活动 JOB（`RUNNING` / `PAUSED` / `STOPPING`）；`loads` 列所有已加载对象。
- 无 JOB 时输出 `(no jobs)`。

## 4.8 waitjob

```
waitjob FEED
waitjob FEED timeout 30s
waitjob FEED timeout 0
```

- 语义与 `wait task`（§2.5）一致，仅限定等待对象为 JOB 类任务。
- 超时 → `ERR 1004`，不改变 JOB 状态，不自动发 stop。
- 若 JOB 已结束，立即返回其结束状态与退出码。

## 4.9 Task 参数配置（三层覆盖）

```
编译期全局默认  <  jobcfg <name> 按名配置  <  load / run 单次参数
```

- 全局默认：`prio = 2`（低于 `uart_rx` / `timer`，高于 idle）、`stack = 2048 B`。
- 按名配置：`jobcfg FEED prio 3 stack 4096`（扩展命令），存于静态 profile 表。
- 单次覆盖：`run feed.ms prio 3 stack 4096`。
- `ps` / `job` 详情显示实际生效值及来源（`global` / `profile` / `inline`）。
- 约束：栈值超 `[SHELL_JOB_STACK_MIN, SHELL_JOB_STACK_MAX]` 报 `ERR 1001`；优先级高于 `uart_rx` 时 `WARN` 并钳位。

---

# 5. 表达式语法

Shell 与脚本共用一套表达式，类似 C 语言。

## 5.1 数据类型

| 类型 | 示例 | 内部表示 |
|---|---|---|
| INTEGER | `100` `-5` `0xFF` | `int32_t` |
| FLOAT | `12.5` `-0.5` `1e3` | `float`（`SHELL_USE_DOUBLE` 可切双精度） |
| BOOLEAN | `TRUE` `FALSE` `ON` `OFF` `1` `0` | 独立类型，`TRUE/ON`≡真，`FALSE/OFF`≡假 |
| STRING | `"FEED"` | 只读字符串，最大 `SHELL_LINE_MAX` |
| VARIABLE | `$SPEED` | 引用当前上下文变量表 |

## 5.2 带单位数值字面量

```
<number><unit>      例如 100ms / 2s / 30s / 100rpm / 10000Hz / 50% / 500steps
```

| 单位族 | 允许后缀 | 归一化 | 适用语句 |
|---|---|---|---|
| 时间 | `ms` `s` `min` | 统一为毫秒整数 | `wait` / `WAIT TIME` / `TIMEOUT` / `DEC` |
| 转速 | `rpm` | 整数 rpm | `STEP ... SPEED` |
| 频率 | `Hz` `kHz` | 统一为 Hz | `PWM ... FREQ` |
| 占空比 | `%` | 0–100 整数 | `PWM ... DUTY` |
| 位置 | `steps` `deg` `mm` | `deg` / `mm` 按设备配置的传动比换算为 steps | `STEP ... MOVE` / `HOME` |
| 无后缀 | — | 按语句默认单位 | 如 `WAIT TIME 100` 按 ms |

- 单位后缀忽略大小写（`100MS` ≡ `100ms`）。
- 越界（如 `DUTY 150%`、`SPEED -1rpm`）→ `ERR 1001: value out of range`。
- 需要传动比换算而设备未配置时 → `ERR 1003: device calibration missing`。
- 裸数值的默认单位由每条语句在 §12 中声明，`help units` 可查。

## 5.3 运算符（优先级从高到低）

| 优先级 | 运算符 | 结合性 | 说明 |
|---|---|---|---|
| 1 | `( )` | — | 括号 |
| 2 | `NOT` `!` `-` | 右 | 逻辑非、一元负号 |
| 3 | `*` `/` `%` | 左 | 乘除取余 |
| 4 | `+` `-` | 左 | 加减 |
| 5 | `<` `<=` `>` `>=` | 左 | 比较 |
| 6 | `==` `!=` | 左 | 相等 |
| 7 | `AND` `&&` | 左 | 逻辑与（**短路**） |
| 8 | `OR` `\|\|` | 左 | 逻辑或（**短路**） |

```
1 + 2 * 3                       → 7
(1 + 2) * 3                     → 9
7 / 2                           → 3      (int / int 截断)
7 / 2.0                         → 3.5
$SPEED * 2                      → 变量参与
($A > 10) AND ($B < 5)          → BOOLEAN
NOT $ENABLE                     → 取反
$COUNT % 2 == 0                 → 偶数判断
```

## 5.4 类型规则（V1.1 新增）

| 运算 | 规则 |
|---|---|
| 算术 `+ - * /` | int×int → int（`/` 截断）；任一为 float → float；BOOLEAN 或 STRING 参与 → `ERR 1010` |
| 取余 `%` | 仅 int×int；右操作数为 0 → `ERR 1011` |
| 比较 `< <= > >=` | 仅数值（int / float 可混合）；STRING 参与 → `ERR 1010` |
| 相等 `== !=` | 同类型比较；int/float 可混合；STRING 仅与 STRING；STRING 与数值 → `ERR 1010`（不隐式转换） |
| 逻辑 `AND OR NOT` | 要求 BOOLEAN；允许 int/float 按 0=假、非 0=真隐式转换（兼容 `WHILE 1`）；STRING → `ERR 1010` |
| 一元 `-` | 仅数值 |
| 布尔归一化 | `TRUE` `ON` `1` → 真；`FALSE` `OFF` `0` → 假；`print` 输出 `TRUE` / `FALSE` |
| 短路求值 | `AND` 左侧为假时**不求值**右侧；`OR` 左侧为真时不求值右侧。可用于避免未定义变量 |
| 括号不匹配 | 解析期 `PARSE L<n>: unmatched parenthesis` |

## 5.5 求值错误

| 情况 | 错误 |
|---|---|
| 除零 / 取余零 | `ERR 1011: divide by zero` / `modulo by zero` |
| 变量未定义 | `ERR 1009: undefined variable $X` |
| 类型不匹配 | `ERR 1010: type mismatch` |
| 括号不匹配 / 语法错误 | `PARSE L<n>: ...`（源码 1012） |
| 数值越界 / 单位非法 | `ERR 1001: invalid value` |
| 只读变量赋值 | `ERR 1013: variable is read-only` |

---

# 6. 控制语句

控制语句属于 MotionScript 脚本语法，交互态与脚本中均可用。关键字忽略大小写。

## 6.1 IF / ELSE / END

```
IF <expr>
    <statements>
[ELSE
    <statements>]
END
```

```
IF $VALUE > 100
    GPIO 0 ON
ELSE
    GPIO 0 OFF
END
```

- `END` **只结束 IF**，不结束 JOB / FOR / WHILE / LOOP。
- 最大嵌套 `SHELL_BLOCK_DEPTH`（默认 8）层，超限 `ERR 1017`。
- `ELSE` 要求当前栈顶为 `IF`，否则 `PARSE`。

## 6.2 FOR

```
FOR $VAR = <start> TO <end> [STEP <step>]
    <statements>
ENDFOR
```

```
FOR $I = 0 TO 9
    GPIO 0 ON
    WAIT TIME 100ms
    GPIO 0 OFF
ENDFOR

FOR $I = 10 TO 1 STEP -1
    SHELL PRINT $I
ENDFOR
```

- 循环变量为 **JOB 级变量**（无块级作用域），循环外可见，保留最后值。
- 上下界与 `STEP` 在进入时**求值一次**；循环体内修改边界变量不影响本轮。
- 默认 `STEP 1`；`STEP 0` → `ERR 1001`（解析期可检出常量情形）。
- 结束值规则：正常结束为**首次越界值**（`FOR $I = 0 TO 9` 后 `$I = 10`）；`BREAK` 保留当前值；`CONTINUE` **先执行 STEP 更新**再判定；循环体一次未进入则保持循环前的值。

## 6.3 WHILE

```
WHILE <expr>
    <statements>
ENDWHILE
```

```
WHILE $COUNT > 0
    ADC 0 READ, TO $VALUE
    IF $VALUE > 3000
        BREAK
    END
    SET $COUNT = $COUNT - 1
    WAIT TIME 100ms
ENDWHILE
```

- 空循环体（`WHILE TRUE ... ENDWHILE` 无 WAIT）会在每条语句间隐式 `yield`，但**不得**忙等；lint 输出 `WARN: tight loop without WAIT`。

## 6.4 BREAK / CONTINUE

- 只作用于最近一层 `FOR` / `WHILE` / `LOOP`。
- 循环外使用 → 解析期 `PARSE L<n>: BREAK outside loop`。
- 检查在解析期（lint）完成，不需要运行到该行才报错。

## 6.5 兼容 LOOP

```
LOOP N   ≡  FOR $__I = 1 TO N
LOOP     ≡  WHILE TRUE
```

对应结束关键字 `ENDLOOP`。`$__I` 为保留变量，不允许脚本显式赋值（`ERR 1013`）。

## 6.6 块结束关键字

| 起始 | 结束 | 备注 |
|---|---|---|
| `IF` | `END` | `ELSE` 不压栈 |
| `FOR` | `ENDFOR` | |
| `WHILE` | `ENDWHILE` | |
| `LOOP` | `ENDLOOP` | |
| `JOB` | `ENDJOB` | 脚本最外层 |
| `ON ERROR` | `ENDERROR` | 钩子 |
| `ON STOP` | `ENDSTOP` | 钩子（V1.1 新增） |

## 6.7 块栈、续行与静态检查

### 6.7.1 块栈算法

```
block_stack[SHELL_BLOCK_DEPTH]，元素 = {type, line}
压栈：JOB / IF / FOR / WHILE / LOOP / ON ERROR / ON STOP
弹栈：ENDJOB / END / ENDFOR / ENDWHILE / ENDLOOP / ENDERROR / ENDSTOP
特殊：ELSE 要求栈顶 = IF，且不改变栈深
```

- 续行态 ⇔ 栈非空；提示符 `...>`，深度 = 栈深（显示为 `...>` 即可，深度可由 `job` 详情 `CallStk` 查）。
- 续行期间上下文恒为 `VM_CTX`，**不做 Shell 命令查表**（§1.5）。
- 结束关键字与栈顶类型不匹配 → `PARSE L<n>: unmatched <ENDxxx>`，整块丢弃。
- 单独一行 `.` → 清空块栈、丢弃缓冲，回到 `>`，不创建 Task。

### 6.7.2 提交语义

- 若栈底为 `JOB` 且起始上下文为 `SHELL_CTX`（交互态直接敲入的 `JOB ... ENDJOB` 块）→ 等价一次**前台** `run`（末尾可加 `&` 转后台），创建独立 JOB Task。
- 若块不含 `JOB` 外层（如单独的 `IF ... END`）→ 转为一个**匿名交互 JOB**，执行完即销毁，输出前缀 `[JOB @interactive]`。
- 若已在 `VM_CTX`（脚本文件内 / 某个 JOB 内）→ 直接投递给当前 VM，不新建 Task。
- 块闭合后先执行静态检查，失败则不提交。

### 6.7.3 lint / 静态检查项

`run <file> lint`、`load`、块提交时均执行：

1. 块配对与嵌套深度 ≤ `SHELL_BLOCK_DEPTH`；
2. `BREAK` / `CONTINUE` 位于循环栈内；
3. `LABEL` 名唯一、`GOTO` 目标存在且不跳入块内；
4. `CALL` / `RETURN` 配对、调用栈深度 ≤ 8；
5. `ON ERROR` / `ON STOP` 在 JOB 块内且各最多一个；
6. 常量表达式的除零、单位越界；
7. VM 内裸 `START <name>` / `STOP <name>`（必须带 `JOB`）；
8. `WAIT JOB` 环形依赖（跨脚本时运行期再检）。

输出格式 `PARSE L<n>: <message>` 或 `WARN L<n>: <message>`；`lint` 模式下有 `PARSE` 即**不启动**。

## 6.8 ON ERROR / ON STOP（V1.1 新增）

```
JOB FEED
ON ERROR
    SHELL LOG "err=" + $ERRCODE + " @" + $ERRLINE
    STEP 0 STOP, DEC 100ms
ENDERROR

ON STOP
    SHELL LOG "stopped by operator"
    STEP 0 STOP, DEC 500ms
ENDSTOP

    STEP 0 HOME, SPEED 100rpm
    ...
ENDJOB
```

| 项 | `ON ERROR` | `ON STOP` |
|---|---|---|
| 触发 | 运行时错误（1001/1003/1004/1005/1006/1007/1011…） | `stop` / `stop force` / Ctrl-C / 前台中断；`ENDJOB` 时可选执行 |
| 进入前置变量 | `$ERRCODE` `$ERRLINE` `$ERRMSG` | `$ERRCODE = 1008`、`$ERRLINE` = 当前行、`$ERRMSG = "job stopped"` |
| 结束状态 | `ERROR` | `ABORTED`（`ENDJOB` 触发时为 `COMPLETED`） |
| 声明位置 | JOB 块内任意位置，**最多一个**，解析期注册 | 同左 |

**规则：**

- 钩子内再次出错**不递归触发**（置 `in_handler` 标志），直接跳到 cleanup，并输出 `WARN: error in ON ERROR handler`。
- `kill` **不触发**任何钩子（§3.3）。
- `stop <name> force` 若 JOB 未定义 `ON STOP`，退化为执行 `ON ERROR`（兼容 V1.0 行为），`$ERRCODE = 1008`。
- 钩子执行期间不响应新的 `stop`；`kill` 仍可强杀。
- 钩子结束后由内核执行 cleanup（§4.3），脚本无需自己释放设备。
- V1.2 候选：`RESUME ERROR`（跳过出错语句继续）；V1.1 不提供。

## 6.9 跳转与子程序（V1.1 新增）

```
LABEL <id>          定义标签，JOB 内唯一（大小写折叠）
GOTO <id>           无条件跳转
CALL <id>           压调用栈并跳转（深度 ≤ 8）
RETURN              弹调用栈返回
```

- `LABEL` 名属于标识符，忽略大小写；重复定义 → `PARSE L<n>: duplicate label`。
- `GOTO` **禁止跳入** `IF` / `FOR` / `WHILE` / `LOOP` / 钩子块内部（解析期检查 `PARSE: jump into block`）；允许跳出到外层。
- `GOTO` 目标不存在 → `PARSE: undefined label`；跨 JOB 跳转禁止。
- `CALL` 压栈，`RETURN` 弹栈；`RETURN` 时栈空 → `PARSE: RETURN without CALL`。
- 调用栈与块栈共用深度上限 `SHELL_BLOCK_DEPTH`；超限 `ERR 1017`。
- `job <name>` 的 `CallStk` 字段显示当前栈（如 `FEED → HOME (depth 1)`）。
- `WAIT JOB <name>` 运行期检测环形依赖，成环 → `ERR 1020: circular job wait`。

## 6.10 脚本内调 Shell 能力

```
SHELL PRINT "message"
SHELL PRINT $VAR
SHELL LOG "message"
SHELL STATE TO $S
SHELL MEM                 ; 白名单内的扩展命令透传
SHELL PS
```

- 在 JOB Task 内**同步执行**，输出带 `[JOB <name>]` 前缀（§8.4 保证原子成行）。
- `SHELL STATE TO $S` 返回只读字符串，格式 `key=value;...`（与 `state` 同源，便于脚本解析）。
- 透传白名单：`ver` `mem` `ps` `state` `help` 及显式标记为 `CMD_REENTRANT` 的扩展命令；其余拒绝（`ERR 1016`）。
- 白名单命令不得阻塞、不得修改 JOB 生命周期、不得操作设备。

---

# 7. 扩展命令

## 7.1 机制

- 扩展命令与基础命令**同机制**，注册到 `shell_cmd` 段，`help` 自动收录（按 `group` 分组）。
- `ps` 看到的任务与扩展命令无关——扩展命令运行在 **Shell Task 上下文**，除非它自己调 RTOS API 创建任务。
- 查找统一用大小写折叠比较（`strcasecmp`）。
- 查表优先级：**基础命令 > 扩展命令 > 脚本语句**（`SHELL_CTX`）；扩展命令不得覆盖基础命令（注册期拒绝），与脚本关键字同名时启动时输出 `WARN` 但允许（因为 `SHELL_CTX` 下命令优先）。

## 7.2 注册接口

```c
typedef enum {
    CMD_NONE       = 0,
    CMD_DANGER     = (1u << 0),  /* 执行前要求 confirm */
    CMD_HIDDEN     = (1u << 1),  /* help 不列出、Tab 不补全，但可执行 */
    CMD_REENTRANT  = (1u << 2),  /* 允许被 SHELL <cmd> 从 VM_CTX 透传 */
} shell_cmd_flags_t;

typedef int (*shell_cmd_fn)(int argc, char **argv);

typedef struct shell_cmd {
    const char *name;
    shell_cmd_fn handler;
    const char *usage;
    const char *help;
    const char *group;      /* help 分组，如 "task" / "device" / "diag" */
    uint32_t    flags;
} shell_cmd_t;

/* 基础宏：flags = 0 */
#define SHELL_CMD_EXPORT(name, fn, usage, help) \
    SHELL_CMD_EXPORT_EX(name, fn, usage, help, "ext", CMD_NONE)

/* 带完整属性的宏 */
#define SHELL_CMD_EXPORT_EX(name, fn, usage, help, group, flags)       \
    __attribute__((used, section("shell_cmd"), aligned(4)))            \
    static const shell_cmd_t __shell_cmd_##name = {                    \
        #name, fn, usage, help, group, flags                           \
    }

#define SHELL_CMD_EXPORT_DANGER(name, fn, usage, help) \
    SHELL_CMD_EXPORT_EX(name, fn, usage, help, "danger", CMD_DANGER)

#define SHELL_CMD_EXPORT_HIDDEN(name, fn, usage, help) \
    SHELL_CMD_EXPORT_EX(name, fn, usage, help, "ext", CMD_HIDDEN)
```

**返回值约定**：`0` 成功；`-<errno>` 为 §8.3 错误码（如 `return -1002;`）；框架负责转成 `ERR <code>: <message>` 输出。提供辅助宏 `SHELL_ERR(code)` 与 `SHELL_OK`。

**注册期校验**（启动时遍历命令段）：

| 检查 | 失败行为 |
|---|---|
| 名称唯一（折叠比较） | 启动 `WARN`，保留先注册者 |
| 名称不含空白 / `;` / `"`、长度 ≤ 16 | 启动 `WARN`，跳过该命令 |
| 与基础命令同名 | 启动 `WARN`，跳过该命令（基础命令优先） |
| 与脚本关键字同名 | 启动 `WARN`，允许存在 |
| `handler` 非空 | 启动 `WARN`，跳过 |

## 7.3 链接脚本（必须有，V1.0 遗漏）

```ld
/* GCC ld / ARMClang scatter 等价写法 */
SECTIONS
{
    .shell_cmd : ALIGN(4)
    {
        __shell_cmd_start = .;
        KEEP(*(shell_cmd))
        __shell_cmd_end = .;
    } > FLASH
}

/* C 侧声明 */
extern const shell_cmd_t __shell_cmd_start[];
extern const shell_cmd_t __shell_cmd_end[];
```

- `KEEP` 必需，否则链接器在 `--gc-sections` 下会丢弃未显式引用的段。
- `aligned(4)` 必需，保证按结构体步长遍历不越界。
- 若 RTOS 使用自定义启动（无标准 `ld`），需提供等价的段边界符号，或用 `SHELL_CMD_REGISTER()` 构造函数表替代。

## 7.4 示例

```c
/* 扩展命令：查看堆 */
static int cmd_mem(int argc, char **argv)
{
    shell_printf("heap: %u used / %u total\n",
                 rtos_heap_used(), rtos_heap_total());
    return SHELL_OK;
}
SHELL_CMD_EXPORT_EX(mem, cmd_mem,
    "mem                     查看堆使用",
    "打印 RTOS 堆统计", "diag", CMD_REENTRANT);

/* 扩展命令：按名覆盖 JOB Task 参数 */
static int cmd_jobcfg(int argc, char **argv)
{
    if (argc < 6) {
        shell_puts("usage: jobcfg <name> prio <p> stack <n>\n");
        return -1001;
    }
    if (job_profile_set(argv[1], shell_atoi(argv[3]),
                        shell_atoi(argv[5])) < 0)
        return -1001;
    shell_printf("profile %s updated\n", argv[1]);
    return SHELL_OK;
}
SHELL_CMD_EXPORT(jobcfg, cmd_jobcfg,
    "jobcfg <name> prio <p> stack <n>   配置 JOB 优先级与栈",
    "按 JOB 名覆盖 Task 参数，优先级低于 load/run 的单次参数");

/* 扩展命令：创建自定义任务 */
static int cmd_spawn(int argc, char **argv)
{
    if (argc < 3) {
        shell_puts("usage: spawn <name> <prio>\n");
        return -1001;
    }
    rtos_task_create(argv[1], demo_task, NULL,
                     shell_atoi(argv[2]), 1024);
    shell_printf("task %s created\n", argv[1]);
    return SHELL_OK;
}
SHELL_CMD_EXPORT(spawn, cmd_spawn,
    "spawn <name> <prio>      创建演示任务",
    "创建一个运行 demo_task 的 RTOS 任务，TYPE=SYS，可用 ps 查看");

/* 危险命令示例 */
static int cmd_wipe(int argc, char **argv)
{
    fs_format();
    return SHELL_OK;
}
SHELL_CMD_EXPORT_DANGER(wipe, cmd_wipe,
    "wipe confirm            格式化脚本分区",
    "危险：删除所有脚本文件，需 confirm");
```

## 7.5 扩展命令与 Task 的关系

- 扩展命令在 Shell Task 内执行，**不创建新任务**。
- 扩展命令内可调 RTOS API 创建 / 删除任务，此类任务 `ps` 可见（`TYPE=SYS`，除非显式标记为 JOB）。
- 需要长期运行的逻辑 → 扩展命令创建任务，而非阻塞 Shell；若阻塞 Shell 超过 `SHELL_WAIT_DEFAULT_MS`，输出 `WARN: shell blocked`。
- `CMD_REENTRANT` 标记的命令必须保证可重入（只读或自带锁），否则不得被 `SHELL <cmd>` 透传。

## 7.6 危险命令

设置 `CMD_DANGER` 的命令（含基础命令 `reboot`）执行前要求 `confirm`，框架见 §3.6：

```
kill timer confirm
pause uart_rx confirm
reboot confirm
wipe confirm
```

非危险命令带 `confirm` → 执行 + `WARN: confirm ignored`。

---

# 8. 错误与输出

## 8.1 输出函数

```c
void shell_printf(const char *fmt, ...);
void shell_puts(const char *s);
void shell_putchar(char c);
void shell_printf_job(const char *job, const char *fmt, ...);  /* 带 [JOB x] 前缀，原子成行 */
```

- 所有输出统一走 `shell_printf`，可重定向 UART / USB / RTT；后端由 `shell_output_backend_t` 配置（§14.2）。
- `shell_putchar` 仅用于行编辑回显，不参与日志输出路径。

## 8.2 错误格式

```
ERR <code>: <message> [@JOB <name> L<line>]
PARSE L<line>: <message>
WARN L<line>: <message>
```

```
ERR 1011: divide by zero
ERR 1004: WAIT STEP 0 timeout @JOB FEED L12
ERR 1002: task FOO not found
PARSE L23: unmatched END
WARN L5: variable $X used before set
WARN: confirm ignored
```

- `@JOB <name> L<line>` 仅在错误来自 JOB Task 时附加；Shell 上下文错误不带。
- `PARSE` / `WARN` 的行号在解析期产生，行号从 1 开始（脚本文件行 / 粘贴块内相对行）。

## 8.3 错误码

| 码 | 说明 | 典型 message |
|---|---|---|
| 1001 | 参数 / 表达式 / 取值错误 | `invalid argument for wait`、`value out of range` |
| 1002 | 任务 / 设备 / 文件不存在 | `task FOO not found`、`file not found`、`file system not available` |
| 1003 | 设备执行失败 | `STEP0 home failed`、`device calibration missing` |
| 1004 | 等待超时 | `WAIT STEP 0 timeout`、`wait task FEED timeout` |
| 1005 | 传感器错误 | `SENSOR0 no data` |
| 1006 | 电机错误 | `STEP0 stall detected` |
| 1007 | 限位触发 | `limit switch +X active` |
| 1008 | 程序执行错误 | `job stopped`（仅用于 `stop force` 兼容路径） |
| 1009 | 变量未定义 | `undefined variable $X` |
| 1010 | 类型不匹配 | `type mismatch` |
| 1011 | 除零 / 取余零 | `divide by zero` |
| 1012 | 词法 / 语法错误（`PARSE` 同源码） | `unexpected token` |
| 1013 | 变量只读 / 重复定义 | `variable is read-only` |
| 1014 | 设备忙 / 已被独占 | `STEP0 busy (owner: JOB FEED)` |
| 1015 | 设备未申请 / 未占用 | `STEP0 not claimed` |
| 1016 | 需要危险确认 / 越权 | `dangerous command requires confirm` |
| 1017 | 资源耗尽 | `paste buffer overflow`、`block depth exceeded`、`line too long` |
| 1018 | 名称冲突 / 保留名 | `job FEED already running`、`reserved name` |
| 1019 | 当前状态不允许 | `job FEED is not LOADED`、`job FEED is RUNNING` |
| 1020 | 非法跳转 / 环形等待 | `circular job wait` |

- `SHELL_LEGACY_ERRCODES = 1` 时把 1011→1001、1014/1018/1019→1002、1017→1008 映射回 V1.0 码，仅供已有上位机过渡。
- `SHELL_ERR_*` 常量在 `shell_err.h` 中定义，扩展命令返回 `-SHELL_ERR_XXX`。

## 8.4 并发输出（V1.1 新增）

多个 JOB Task 可能同时调用 `shell_printf`。规范如下：

- `shell_printf` 内部持有 `shell_out_mutex`（RTOS mutex，或短临界区）；**单次调用为原子输出**，不会被其他 Task 插入。
- 一行 `[JOB FEED] ...` 必须由**一次** `shell_printf_job()` 调用完成，禁止拆成"打印前缀"+"打印正文"两次调用。
- 输出后端为慢速 UART 时，使用可配置环形缓冲 `SHELL_OUT_BUF`；缓冲满时的策略由 `SHELL_OUT_OVERFLOW` 决定（`drop_newest` / `drop_oldest` / `block`），默认 `drop_oldest` 且输出 `WARN: output overflow`。
- 禁止在中断上下文调用 `shell_printf`；ISR 通过 `shell_log_from_isr()` 投递到缓冲。
- 互斥锁持有期间不得调用可能阻塞的设备 API，避免优先级反转。

---

# 9. 交互细节

## 9.1 提示符

```
>           主提示符（SHELL_CTX）
...>        续行（块栈非空，VM_CTX）
(dbg)>      调试模式
[JOB FEED]  脚本输出前缀
```

## 9.2 行编辑

| 按键 | 功能 |
|---|---|
| `Backspace` / `DEL` | 删字符 |
| `Ctrl-C` | 中断（§9.3） |
| `Ctrl-D` | 粘贴结束 / EOF（等价 `run` 当前缓冲） |
| `Tab` | 补全（命令 / 任务名 / JOB 名 / 标签 / 变量 / 文件） |

- `CMD_HIDDEN` 命令不参与 Tab 补全。
- `SHELL_HAS_FS = 0` 时不补全文件名。
- 多个候选时第一次 Tab 补公共前缀，第二次 Tab 列出候选。

## 9.3 Ctrl-C 语义

| 场景 | 行为 |
|---|---|
| 无前台 JOB | 清空当前行 |
| 有前台 JOB | 对该 JOB 发 `stop`（协作式，执行 `ON STOP`） |
| `wait <time>` / `wait task` / `waitjob` 中 | 中断等待，返回 `WARN: wait interrupted` |
| 批量粘贴中 | 终止粘贴，已提交的行不回滚 |
| 有后台 JOB 但无前台 | 只清空当前行，**不影响后台 JOB** |

## 9.4 多行块

- 行首 `JOB` / `IF` / `FOR` / `WHILE` / `LOOP` / `ON ERROR` / `ON STOP` 进入续行（块栈算法见 §6.7.1）。
- 续行期间上下文恒为 `VM_CTX`，只按脚本语句处理。
- 块闭合后先 lint、再提交（§6.7.2）。
- 单独一行 `.` 放弃当前块。
- 嵌套上限 8，超限 `ERR 1017: block depth exceeded`。

## 9.5 批量粘贴

- 按 `\n` 切分逐行判定；每行独立走 §1.4 的判定顺序，但块状态跨行保持。
- 缓冲上限 8192 字节，超出报 `ERR 1017: paste buffer overflow`。
- `Ctrl-D` 结束，等价 `run`（执行当前粘贴缓冲区），执行后清空缓冲。
- 粘贴期间的错误不终止后续行，除非是 `PARSE`（块结构错误使后续无法解析）。

## 9.6 输出与回显

- 前台 JOB 运行期间，Shell 不再回显用户输入（输入缓冲被丢弃），但 `SHELL PRINT` 与设备日志正常输出。
- `echo` 输出不带前缀；`[JOB <name>]` 前缀只用于 JOB Task 内产生的输出。

---

# 10. 命令速查

## 10.1 基础命令

| 命令 | 语法 | 说明 |
|---|---|---|
| `help` | `help [topic]` | 帮助（分组收录扩展命令） |
| `ver` | `ver` | 版本与构建信息 |
| `echo` | `echo <text>` | 回显，仅 `$VAR` 替换，不求值 |
| `reboot` | `reboot [confirm]` | 软复位（危险） |
| `print` | `print <expr>` | 求值打印 |
| `set` | `set $VAR = <expr>` | 设 Shell 变量 |
| `unset` | `unset $VAR` | 删 Shell 变量 |
| `wait` | `wait <time>` / `wait task <name\|#tid> [timeout <t>]` | 阻塞等待 |
| `load` | `load <file> [as <name>] [inherit] [prio <p>] [stack <n>]` | 加载，JOB → LOADED |
| `unload` | `unload <name>` | 卸载非运行态 JOB |
| `loads` | `loads` | 列已加载 JOB |
| `run` | `run [<file>] [&] [as <name>] [with $V...] [lint]` | 加载并启动（默认前台） |
| `state` | `state` | 系统状态总览 |

## 10.2 任务管理

| 命令 | 语法 | 说明 |
|---|---|---|
| `ps` | `ps [all\|job\|sys\|name <name>\|#<tid>\|-a]` / `ps <name>` | 列 RTOS 任务（固定 8 列） |
| `kill` | `kill <task\|#tid> [confirm]` | 删除任务 |
| `pause` | `pause <task\|#tid> [confirm]` | 挂起任务 |
| `resume` | `resume <task\|#tid>` | 恢复任务 |

## 10.3 JOB 管理

| 命令 | 语法 | 说明 |
|---|---|---|
| `jobs` | `jobs` | 列活动 JOB |
| `job` | `job <name>` | JOB 详情 |
| `start` | `start <name>` | 启动 JOB（LOADED 或已结束） |
| `stop` | `stop <name> [force]` | 协作式 / 强制停止 |
| `fg` | `fg <name>` | 附着为前台 |
| `waitjob` | `waitjob <name> [timeout <t>]` | 等待 JOB |

## 10.4 扩展命令

通过 `SHELL_CMD_EXPORT*` 注册，`help` 自动按 `group` 收录；`ps` 不显示（扩展命令不创建任务，除非自行创建）。内建示例：`mem` `spawn` `jobcfg` `wipe(danger)`。

## 10.5 脚本关键字

| 类别 | 关键字 |
|---|---|
| 设备 | `STEP` `PWM` `GPIO` `ADC` `SENSOR` `RELAY` `CLAIM` `RELEASE` |
| 流程 | `IF` `ELSE` `END` `FOR` `ENDFOR` `WHILE` `ENDWHILE` `LOOP` `ENDLOOP` `BREAK` `CONTINUE` |
| 跳转 | `LABEL` `GOTO` `CALL` `RETURN` |
| 任务 | `JOB` `ENDJOB` `START JOB` `STOP JOB` `WAIT TIME` `WAIT STEP` `WAIT GPIO` `WAIT JOB` |
| 异常 | `ON ERROR` `ENDERROR` `ON STOP` `ENDSTOP` |
| 变量 | `SET` `UNSET` |
| 内建 | `SHELL PRINT` `SHELL LOG` `SHELL STATE` `SHELL <whitelisted>` |

---

# 11. 完整示例（V1.1 自洽版）

```
> ver
IoT Shell v1.1
Build: 2026-11-10
RTOS: <your-rtos> / 3.2.1
VM: per-job task model
Devices: STEP(4) PWM(2) GPIO(8) ADC(4) SENSOR(2) RELAY(2)
FS: enabled | CPU stats: disabled

> ps
TID  NAME      TYPE   STATE      PRIO  STACK   CPU%   JOB
---  --------  -----  ---------  ----  ------  -----  ----
  1  shell     SYS    READY         3   1024   -      -
  2  idle      SYS    RUNNING       0    256   -      -
  3  timer     SYS    READY         5    512   -      -
  4  uart_rx   SYS    BLOCKED       4    512   -      -

> load feed.ms as FEED
loaded FEED from feed.ms (24 lines, lint OK)

> jobs
(no active jobs)

> loads
NAME  STATE   SCRIPT    PC  TASK  VARS
FEED  LOADED  feed.ms   -   -     (empty)

> run feed.ms &
[JOB FEED] Task created (TID 5, prio 2, stack 2048, JobID 3)
[JOB FEED] STEP 0 HOME, SPEED 100rpm
[JOB FEED] WAIT STEP 0
[JOB FEED] PWM 0 START, FREQ 10000Hz, DUTY 50%
[JOB FEED] FOR $I = 0 TO 9

> ps
TID  NAME      TYPE   STATE      PRIO  STACK   CPU%   JOB
---  --------  -----  ---------  ----  ------  -----  ----
  1  shell     SYS    READY         3   1024   -      -
  2  idle      SYS    RUNNING       0    256   -      -
  3  timer     SYS    READY         5    512   -      -
  4  uart_rx   SYS    BLOCKED       4    512   -      -
  5  FEED      JOB    RUNNING       2   2048   -      FEED

> run mix.ms as MIX &
[JOB MIX] Task created (TID 6, prio 2, stack 2048, JobID 4)
[JOB MIX] STEP 1 START, DIR 0, SPEED 500rpm

> pause MIX
Task MIX → SUSPENDED, JOB MIX → PAUSED

> ps MIX
Name    : MIX
TID     : 6
JobID   : 4
Type    : JOB
State   : SUSPENDED
Prio    : 2 (source: global)
Stack   : 2048 (used 612, peak 612)
CPU     : -
Job     : MIX
Script  : mix.ms
PC      : L08
CallStk : MIX (depth 0)
Vars    : $SPEED=500
Devices : STEP1
Started : 2026-11-10 12:35:02 (uptime 3.1s)

> resume MIX
Task MIX → READY, JOB MIX → RUNNING

> stop FEED
[JOB FEED] stop requested @L18
[JOB FEED] ON STOP: STEP 0 STOP, DEC 500ms
[JOB FEED] devices released (STEP0, PWM0, GPIO0)
JOB FEED → ABORTED, Task deleted

> fg MIX
(attached to foreground JOB MIX)
^C
stop @JOB MIX L12
[JOB MIX] ON STOP: STEP 1 STOP, DEC 500ms
JOB MIX → ABORTED, Task deleted

> jobs
(no jobs)

> ps
TID  NAME      TYPE   STATE      PRIO  STACK   CPU%   JOB
---  --------  -----  ---------  ----  ------  -----  ----
  1  shell     SYS    READY         3   1024   -      -
  2  idle      SYS    RUNNING       0    256   -      -
  3  timer     SYS    READY         5    512   -      -
  4  uart_rx   SYS    BLOCKED       4    512   -      -

> ps -a
TID  NAME      TYPE   STATE      PRIO  STACK   CPU%   JOB
---  --------  -----  ---------  ----  ------  -----  ----
  1  shell     SYS    READY         3   1024   -      -
  2  idle      SYS    RUNNING       0    256   -      -
  3  timer     SYS    READY         5    512   -      -
  4  uart_rx   SYS    BLOCKED       4    512   -      -
  5  FEED      JOB    DELETED       2   2048   -      FEED    (ABORTED, 12.3s ago)
  6  MIX       JOB    DELETED       2   2048   -      MIX     (ABORTED, 2.1s ago)

> state
RTOS Tasks: 4 (RUNNING=1 READY=2 BLOCKED=1)
JOBs      : 0
Shell Vars: SPEED=1000, COUNT=3
Devices   : all free
Heap      : 10.1 KB used / 32 KB
```

**示例相对 V1.0 的修正点**：`run` 改为 `&` 后台；`ps` 统一 8 列且 CPU% 填 `-`；`stop` 取代 `stop confirm` / `kill confirm`；`state` 的任务数与 `ps` 一致；新增 `load` / `loads` / `fg` / `ps -a` 与 `ON STOP` 收尾输出。

---

# 12. 设备层与设备注册表（V1.1 新增）

> V1.0 只在 §1.1 定义了设备层的存在、在 §11 给了几条示例语句，但 `kill`「释放设备」、JOB 结束「释放设备」、`state` 的 `STEP0→FEED` 全部依赖一个未定义的机制。本章补齐。

## 12.1 注册表模型

```
device_registry[MAX_DEVICES] = {
    type, index, mode, owner_job_id, state, safe_state, claimed_at
}
```

| 字段 | 说明 |
|---|---|
| `type/index` | `STEP0..3` `PWM0..1` `GPIO0..7` `ADC0..3` `SENSOR0..1` `RELAY0..1` |
| `mode` | `EXCLUSIVE`（写/配置类，默认）/ `SHARED`（只读类） |
| `owner_job_id` | 独占者的 `job_id`；空闲为 0；SHARED 可有多个读者 |
| `state` | 设备当前运行态（RUNNING / IDLE / FAULT …） |
| `safe_state` | 见 §13.3，cleanup 时强制回到该状态 |

**占用规则：**

| 设备 | 默认模式 | 说明 |
|---|---|---|
| `STEP` `PWM` `RELAY` | `EXCLUSIVE` | 同一时刻仅一个 JOB 可控制 |
| `GPIO` | `EXCLUSIVE` | 按 index 独占，避免输出打架 |
| `ADC` `SENSOR` 读 | `SHARED` | 多个 JOB 可并发读；配置类操作升级为独占 |

- **隐式申请**：脚本首次执行某设备的语句时自动申请，失败报 `ERR 1014: <dev> busy (owner: JOB <name>)`。
- **显式申请**：`CLAIM STEP0 PWM0`（脚本开头一次性预占，避免运行到一半才发现冲突）；`RELEASE STEP0` 主动释放。
- **释放时机**：JOB 结束任意方式（`ENDJOB` / error / stop / kill / Task 崩溃）时，由内核 `job_release_devices(job_id)` 统一释放 **owner = 该 job_id 的全部条目**，脚本无需也不能自己保证。
- `state` 的 `Devices` 行与 `ps <name>` / `job <name>` 的 `Devices` 字段均来自注册表实时快照。
- 对未申请的设备执行配置类语句 → `ERR 1015`；只读语句隐式申请即可。

## 12.2 设备语句语法

**通用范式**：

```
<DEVICE> <index> <ACTION> [, <PARAM> <value>]...
```

参数用 `,` 分隔；参数名、设备类型名、动作名忽略大小写；数值可带单位（§5.2）。

### STEP（步进电机）

| 语句 | 参数 | 说明 |
|---|---|---|
| `STEP <n> HOME, SPEED <v>` | `SPEED` 默认 rpm | 回零，阻塞至完成需配 `WAIT STEP` |
| `STEP <n> START, DIR <0\|1>, SPEED <v>` | 默认 rpm | 连续运行 |
| `STEP <n> MOVE <pos>, SPEED <v>` | `pos` 默认 steps | 相对/绝对定位，由配置决定 |
| `STEP <n> STOP, DEC <t>` | `DEC` 默认 ms | 减速停车；`DEC 0` 为立即关断 |
| `STEP <n> SETPOS <pos>` | 默认 steps | 设置当前位置 |

### PWM

| 语句 | 参数 | 说明 |
|---|---|---|
| `PWM <n> START, FREQ <v>, DUTY <v>` | `FREQ` 默认 Hz、`DUTY` 默认 `%` | 启动输出 |
| `PWM <n> DUTY <v>` | 0–100% | 在线改占空比 |
| `PWM <n> STOP` | — | 关断 |

### GPIO / ADC / SENSOR / RELAY

| 语句 | 说明 |
|---|---|
| `GPIO <n> ON` / `GPIO <n> OFF` / `GPIO <n> TOGGLE` | 数字输出；`ON`≡高、`OFF`≡低（按板级配置） |
| `GPIO <n> READ, TO $V` | 读输入，结果 BOOLEAN |
| `ADC <n> READ, TO $V` | 读 ADC 原始值，INTEGER（SHARED） |
| `SENSOR <n> READ, TO $V` | 读传感器工程值，FLOAT（SHARED） |
| `RELAY <n> ON` / `RELAY <n> OFF` | 继电器 |

**错误映射**：设备不存在 → `1002`；被占用 → `1014`；执行失败 / 校准缺失 → `1003`；传感器无数据 → `1005`；电机堵转 → `1006`；限位 → `1007`；参数越界 → `1001`。

## 12.3 CLAIM / RELEASE

```
CLAIM STEP0 PWM0 GPIO0        ; 一次性预占，任一冲突则整组失败并回滚
RELEASE PWM0                  ; 主动释放（后续再用会重新隐式申请）
```

- `CLAIM` 为原子操作：任一设备被占用 → 报 `ERR 1014`，**已申请的全部回滚**，避免部分占用。
- 建议 JOB 开头 `CLAIM` 所有需要的设备，使资源冲突在执行前暴露。
- `RELEASE` 不改变设备物理状态；如需停车须先 `STEP <n> STOP`。

## 12.4 VM 等待语句（可中断，硬约束）

| 语句 | 说明 | 超时 |
|---|---|---|
| `WAIT TIME <t>` | 延时 | 无（由 stop 中断） |
| `WAIT STEP <n> [TIMEOUT <t>]` | 等待运动完成 | 默认 `SHELL_WAIT_DEFAULT_MS`；超时 `1004` |
| `WAIT GPIO <n> <ON\|OFF> [TIMEOUT <t>]` | 等待电平 | 同上 |
| `WAIT JOB <name> [TIMEOUT <t>]` | 等待另一 JOB 结束 | 同上；成环 `1020` |

**实现约束**（对应待定项决策 3）：

```
wait_sem + stop_flag  →  rtos_sem_take(wait_sem, timeout)
stop <name>           →  置 stop_flag 并 rtos_sem_give(wait_sem) 唤醒
被唤醒后              →  不报 1004，直接进入 ON STOP 阶段
```

- 所有 WAIT 必须用 RTOS 阻塞原语实现，**禁止忙等轮询**。
- `WAIT STEP` 同时监听"运动完成"与"stop"两个事件。
- 未显式给 `TIMEOUT` 时取 `SHELL_WAIT_DEFAULT_MS`（默认 30 s）；`TIMEOUT 0` 为无限。
- `WAIT` 被中断后**不执行该语句之后的任何语句**，直接进入钩子与 cleanup。

## 12.5 设备状态查询

```
> state
Devices   : STEP0→FEED, PWM0→FEED, STEP1→MIX, ADC0→(shared: FEED,MIX)
```

`help devices` 列出全部设备类型、动作表、默认单位与占用模式。

---

# 13. 安全与急停（V1.1 新增）

> V1.0 §3.3 只有一句「`kill` 不等于硬件急停」，但运动控制系统必须把软件命令与安全链路的边界写清楚。

## 13.1 能力边界

| 通道 | 覆盖范围 | 说明 |
|---|---|---|
| **硬件急停链路** | 直接切断驱动器使能 / 电机动力电源 | 独立布线，不经过 RTOS / Shell / VM；任何软件故障不得阻断它 |
| **限位输入** | 触发后由驱动层上报 `ERR 1007` | 驱动层先执行物理停车，再通知 JOB |
| **Shell 命令** | `kill` `stop` `reboot` `pause` 等 | **只影响软件任务**，不改变急停链路状态 |
| **内核 cleanup** | STEP 减速停 / PWM 关断 / GPIO 安全电平 / RELAY 断开 | Task 删除前强制执行（§4.3） |

**不变量**：Shell 的任何命令（包括 `reboot confirm`、删除 `timer` 任务）都**不得**屏蔽、模拟或复位硬件急停。急停按下后，Shell 只接收一个只读通知事件，输出 `WARN: ESTOP active, all motion inhibited`。

## 13.2 限位与故障

- 限位触发 → 驱动层立即停车 → JOB 收到 `ERR 1007: limit switch +X active @JOB FEED L12` → 进入 `ON ERROR`（`$ERRCODE=1007`）→ `ERROR` 状态 → cleanup。
- 电机堵转 `1006`、传感器失效 `1005` 走同一路径。
- `ON ERROR` 钩子内**禁止**执行任何使设备继续运动的语句（lint 检查并报 `PARSE: motion statement in error handler`），避免错误处理反而加剧危险。

## 13.3 安全状态表

cleanup 步骤 3 按此表执行，与脚本钩子内容无关：

| 设备 | safe state | 参数 |
|---|---|---|
| `STEP` | 减速停车后关闭使能 | `DEC = SHELL_SAFE_STOP_DEC_MS`（默认 500 ms）；驱动报告故障时改为立即关断 |
| `PWM` | 占空比 0 → 关闭输出 | 立即 |
| `GPIO` | 回到板级配置的安全电平 | 由 `gpio_safe_level[]` 定义，默认 `OFF` |
| `RELAY` | 断开 | 立即 |
| `ADC` / `SENSOR` | 仅释放注册表项 | 只读，无物理动作 |

- 顺序固定：STEP → PWM → RELAY → GPIO → 释放注册表。
- 若某设备的安全收尾失败（如驱动无响应），输出 `ERR 1003: safe stop failed on STEP0` 并置全局 `MOTION_INHIBIT` 标志，直到软复位。

## 13.4 调度与看门狗

- 所有长时间等待必须用 RTOS 阻塞原语（§12.4），**禁止忙等**，否则低优先级任务（含 `uart_rx`）会被饿死，导致无法输入 `stop`。
- VM 在每条脚本语句之间隐式 `yield` 一次；`WHILE TRUE` 空循环被 lint 标 `WARN`。
- Shell Task 与 JOB Task 各自向看门狗注册心跳；JOB Task 心跳超时由看门狗任务强制 `kill` 并触发 cleanup，不依赖 Shell 是否存活。
- `WHILE TRUE` 内的 `WAIT` 同时承担心跳喂狗职责。

## 13.5 禁止事项

- 禁止在中断上下文调用 `shell_printf` / 设备语句。
- 禁止用 `kill timer` / `kill uart_rx` 作为"急停"手段（这两条是软件操作，且 `CMD_DANGER`）。
- 禁止在 `ON ERROR` 钩子内 `GOTO` 回主流程重试运动（V1.1 不支持 `RESUME ERROR`）。

---

# 14. 启动与配置（V1.1 新增）

## 14.1 上电流程

```
1) 硬件 / 驱动初始化，限位与急停输入先于电机使能生效
2) 设备注册表初始化（全部空闲，物理设备置 safe_state）
3) 加载 shell_config（编译期常量 + 可选持久化覆盖）
4) 遍历 shell_cmd 段，执行注册期校验（§7.2）
5) 创建 Shell Task（prio = SHELL_SHELL_PRIO，stack = SHELL_SHELL_STACK）
6) 初始化 Shell 变量表、历史、粘贴缓冲、输出互斥锁
7) 若 SHELL_HAS_FS 且存在 /autoexec.ms → 以【后台】执行（不阻塞 Shell 启动）
8) 打印 banner 与 > 提示符
```

- `autoexec.ms` 解析失败只输出 `PARSE`，不阻止 Shell 启动。
- 启动阶段不自动使能任何 STEP / PWM。

## 14.2 配置结构

```c
typedef struct {
    uint16_t line_max;
    uint16_t paste_max;
    uint8_t  block_depth;
    uint8_t  job_max;
    uint8_t  job_prio;
    uint16_t job_stack;
    uint32_t wait_default_ms;
    uint32_t tid_tombstone_ms;
    uint16_t safe_stop_dec_ms;
    uint8_t  has_cpu_stats;
    uint8_t  has_fs;
    uint8_t  legacy_errcodes;
    uint8_t  output_overflow_policy;
    const shell_output_backend_t *output;
    const uint8_t *gpio_safe_level;   /* 每路 GPIO 的安全电平 */
} shell_config_t;
```

## 14.3 配置常量表

| 配置 | 默认值 | 用途 |
|---|---|---|
| `SHELL_LINE_MAX` | 256 | 行缓冲 |
| `SHELL_PASTE_MAX` | 8192 | 粘贴缓冲 |
| `SHELL_BLOCK_DEPTH` | 8 | 块嵌套 / 调用栈上限 |
| `SHELL_JOB_MAX` | 8 | 并发 JOB 上限 |
| `SHELL_JOB_STACK_MIN/MAX` | 1024 / 16384 | 栈合法区间 |
| `SHELL_JOB_PRIO` | 2 | 默认优先级（§4.9 三层覆盖） |
| `SHELL_JOB_STACK` | 2048 | 默认栈（字节） |
| `SHELL_SHELL_PRIO` / `SHELL_SHELL_STACK` | 3 / 1024 | Shell Task 自身 |
| `SHELL_WAIT_DEFAULT_MS` | 30000 | 默认等待超时 |
| `SHELL_TID_TOMBSTONE_MS` | 5000 | TID 墓碑保留 |
| `SHELL_SAFE_STOP_DEC_MS` | 500 | kill / 异常时 STEP 减速时间 |
| `SHELL_HAS_CPU_STATS` | 0 | CPU% 统计开关；关闭时该列填 `-` |
| `SHELL_HAS_FS` | 1 | 文件系统支持 |
| `SHELL_LEGACY_ERRCODES` | 0 | 错误码兼容开关 |
| `SHELL_OUT_BUF` | 1024 | 输出环形缓冲 |
| `SHELL_OUT_OVERFLOW` | `drop_oldest` | 输出溢出策略 |
| `SHELL_RUN_FOREGROUND` | 1 | `run` 默认前台（0 则默认后台） |

## 14.4 文件系统

- `SHELL_HAS_FS = 1`：脚本存放于只读或可写分区，`run` / `load` 接受文件路径；Tab 补全文件名。
- `SHELL_HAS_FS = 0`：只能使用粘贴缓冲区运行（`Ctrl-D` 或 `run` 无参数），`run <file>` 报 `ERR 1002: file system not available`；若提供脚本槽位，可由 `run <slot>` 扩展命令实现，但需另行定义槽位编号规则。
- `autoexec.ms` 仅在 `SHELL_HAS_FS = 1` 时生效。

---

# 15. 调试模式（入口定义，命令集 V1.2）

- 提示符 `(dbg)>`（§9.1）。
- 进入方式：`dbg <job>`（附着到一个 `PAUSED` 或带断点启动的 JOB）或 `run <file> debug`（以挂起态启动，停在 L1）。
- 实现基础：断点通过 `pause` 机制挂起对应 JOB Task；单步在**指令边界**（每条脚本语句执行完）恢复并再次挂起。
- V1.1 **只定义**提示符与入口，以下命令集列为 V1.2 范围：`s`(step) `n`(next) `c`(continue) `b <line>`(breakpoint) `p <expr>`(print, 在 JOB 变量表中求值) `bt`(调用栈) `q`(退出并 resume)。
- V1.1 中调用 `dbg` 返回 `WARN: debugger not available in V1.1`，不改变 JOB 状态。

---

# 附录 A · 错误码触发点速查

> 权威定义见 §8.3。本表用于实现时定位"哪里该抛这个码"。

| 码 | 主要触发点 |
|---|---|
| 1001 | 参数个数 / 取值越界 / 单位非法 / `STEP 0` 为零 / `FOR STEP 0` / 栈值越界 / 禁止等待 shell、idle |
| 1002 | `ps` / `kill` / `pause` 找不到任务或 TID；`run` / `load` 找不到文件；无 FS；设备 index 越界 |
| 1003 | 设备语句执行失败、校准缺失、cleanup 安全收尾失败 |
| 1004 | `wait task` / `waitjob` / `WAIT STEP` / `WAIT GPIO` / `WAIT JOB` 超时 |
| 1005 | `SENSOR` / `ADC` 读失败、无数据 |
| 1006 | `STEP` 堵转、驱动报警 |
| 1007 | 限位输入触发 |
| 1008 | 仅 `stop force` 兼容路径写入 `$ERRCODE` |
| 1009 | 表达式 / `echo` 引用未定义变量 |
| 1010 | 算术、比较、逻辑的类型不匹配 |
| 1011 | `/` 与 `%` 的右操作数为 0 |
| 1012 | 词法错误、未闭合字符串、`PARSE` 的源码 |
| 1013 | 给只读内建变量 / `$__I` 赋值 |
| 1014 | 隐式申请或 `CLAIM` 时设备被独占 |
| 1015 | 对未申请设备执行配置类语句 |
| 1016 | 危险命令缺 `confirm`；`SHELL <cmd>` 透传非白名单命令 |
| 1017 | 行 / 粘贴缓冲超限、块深 / 调用栈 / JOB 数超限 |
| 1018 | JOB 名冲突、使用保留名 |
| 1019 | 状态机拒绝（`start` 未加载 / 已运行、`unload` 运行中） |
| 1020 | `WAIT JOB` 环形依赖、非法跳转（跨 JOB / 跳入块内） |

---

# 附录 B · 语法 EBNF 摘要

```ebnf
line          = [ statement ] [ comment ] ;
comment       = ";" | "//" , { any_char } ;
statement     = shell_command | script_statement ;

(* ---- Shell 命令 ---- *)
shell_command = cmd_name , { arg } ;
cmd_name      = identifier ;
arg           = token ;
token         = identifier | number | unit_number | string | variable | "#" , number ;

(* ---- 脚本语句 ---- *)
script_statement = device_stmt | control_stmt | job_stmt | var_stmt
                 | wait_stmt | jump_stmt | shell_builtin ;

device_stmt  = device_type , index , action , { "," , param , value } ;
device_type  = "STEP" | "PWM" | "GPIO" | "ADC" | "SENSOR" | "RELAY" ;

control_stmt = if_stmt | for_stmt | while_stmt | loop_stmt
             | "BREAK" | "CONTINUE" ;
if_stmt      = "IF" , expr , { script_statement } ,
               [ "ELSE" , { script_statement } ] , "END" ;
for_stmt     = "FOR" , variable , "=" , expr , "TO" , expr ,
               [ "STEP" , expr ] , { script_statement } , "ENDFOR" ;
while_stmt   = "WHILE" , expr , { script_statement } , "ENDWHILE" ;
loop_stmt    = "LOOP" , [ number ] , { script_statement } , "ENDLOOP" ;

job_stmt     = "JOB" , identifier , { job_body } , "ENDJOB" ;
job_body     = script_statement | error_handler | stop_handler ;
error_handler= "ON ERROR" , { script_statement } , "ENDERROR" ;
stop_handler = "ON STOP" , { script_statement } , "ENDSTOP" ;

var_stmt     = "SET" , variable , "=" , expr
             | "UNSET" , variable
             | "CLAIM" , { device_id }
             | "RELEASE" , { device_id } ;
wait_stmt    = "WAIT TIME" , time_value
             | "WAIT STEP" , index , [ "TIMEOUT" , time_value ]
             | "WAIT GPIO" , index , bool_value , [ "TIMEOUT" , time_value ]
             | "WAIT JOB" , identifier , [ "TIMEOUT" , time_value ] ;
jump_stmt    = "LABEL" , identifier | "GOTO" , identifier
             | "CALL" , identifier | "RETURN" ;
shell_builtin= "SHELL PRINT" , expr | "SHELL LOG" , expr
             | "SHELL STATE TO" , variable | "SHELL" , whitelisted_cmd ;

(* ---- 表达式 ---- *)
expr         = or_expr ;
or_expr      = and_expr , { ( "OR" | "||" ) , and_expr } ;
and_expr     = cmp_expr , { ( "AND" | "&&" ) , cmp_expr } ;
cmp_expr     = add_expr , { ( "==" | "!=" | "<" | "<=" | ">" | ">=" ) , add_expr } ;
add_expr     = mul_expr , { ( "+" | "-" ) , mul_expr } ;
mul_expr     = unary ,   { ( "*" | "/" | "%" ) , unary } ;
unary        = ( "NOT" | "!" | "-" ) , unary | primary ;
primary      = "(" , expr , ")" | number | unit_number | string
             | variable | bool_literal ;
bool_literal = "TRUE" | "FALSE" | "ON" | "OFF" ;

(* ---- 词法 ---- *)
identifier   = letter , { letter | digit | "_" } ;   (* 折叠大小写 *)
variable     = "$" , identifier ;
number       = [ "-" ] , digits , [ "." , digits ] | "0x" , hexdigits
             | digits , [ "e" , [ "+" | "-" ] , digits ] ;
unit_number  = number , ( "ms" | "s" | "min" | "rpm" | "Hz" | "kHz"
                        | "%" | "steps" | "deg" | "mm" ) ;
string       = '"' , { any_char_except_quote | '\"' } , '"' ;
```

> `AND` / `OR` 短路求值见 §5.4；块配对、跳转合法性、嵌套深度由 lint 在解析期校验（§6.7.3），不属于 EBNF 能表达的约束。

---

# 附录 C · 相对 V1.0 的章节映射

| V1.0 | V1.1 | 变化 |
|---|---|---|
| §1 总则 | §1 | 新增 §1.5 双上下文分派、§1.2 大小写边界、§1.3 词法规则 |
| §2 基础命令 | §2 | 新增 `load` / `unload` / `loads` / `fg`；重写 `wait`；明确 `echo` / `print` 边界；`run` 前台默认 |
| §3 系统任务管理 | §3 | 新增 §3.5 命名唯一性与 TID 墓碑、§3.6 危险命令框架；`ps` 固定 8 列 |
| §4 JOB 管理 | §4 | 新增完整状态机、cleanup 顺序、变量表策略、前台/后台、三层 Task 参数配置 |
| §5 表达式 | §5 | 新增 §5.2 单位字面量、§5.4 类型规则与短路求值 |
| §6 控制语句 | §6 | 新增 §6.7 块栈 / lint、§6.8 钩子、§6.9 跳转与子程序 |
| §7 扩展命令 | §7 | 新增 flags 宏、链接脚本、注册期校验、返回值约定、优先级 |
| §8 错误与输出 | §8 | 错误码扩至 1020；新增 §8.4 并发输出 |
| §9 交互细节 | §9 | Ctrl-C 语义随前台/后台修订；新增 §9.6 |
| §10 命令速查 | §10 | 同步全部新增命令与关键字 |
| §11 完整示例 | §11 | 重写为自洽版 |
| §12 差异与待定 | — | 移入《需求完善与决议》，7 项待定全部关闭 |
| — | §12 | **新增** 设备层与设备注册表 |
| — | §13 | **新增** 安全与急停 |
| — | §14 | **新增** 启动与配置 |
| — | §15 | **新增** 调试模式（入口定义） |
| — | 附录 A/B/C | **新增** 错误码触发点 / EBNF / 章节映射 |

---

*本规格与《IoT Shell V1.1 需求完善与决议》配套使用；实现中有任何与本文冲突的默认行为，以本文为准。*

