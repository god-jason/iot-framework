# roshell 语法定义

roshell 是机器人设备的脚本语言，上位机通过串口发送脚本，机器人执行。
语法关键字**大小写敏感，全部为小写**。

## 1. 总体形态

- 脚本按**行**解析，一行一条语句；行号从 1 开始，用于错误定位。
- 关键字必须作为一行的第一个 token 出现，且只认小写形式。
- 变量与表达式由编译宏 `RS_ENABLE_EXPR` 控制（见 §6）。

### 1.1 执行模型：直接解释，不生成字节码

- **源文本即程序**：加载时只做一次轻量扫描（校验块配对、记录函数名/循环
  所在行号），不生成任何指令数组；执行时按行号就地词法分析并解释。
- 运行位置用**行号 PC** 表示；`break`、函数调用、`return`、循环回跳都通过
  行号 + 深度扫描定位目标，而非预回填的指令下标。
- 循环/条件嵌套由运行期**块帧栈**（每层仅存类型 + 起始行 + 循环状态，
  ≤16 字节/层）跟踪，深度上限即嵌套上限。

### 1.2 内存策略（尽可能节省）

- 常驻内存只有三部分：**源文本缓冲** + **块帧栈** + **变量表**；
  无 code 数组、无 token 数组缓存（行 token 在栈上临时分配，单行复用）。
- 源文本按需持有：交互式输入逐行执行不留存全文；`load`/`run` 命令的脚本执行
  结束即释放文本内存。
- 一切上限可编译期裁剪（见 §7），默认配置下 20K RAM 设备可运行。

## 2. 词法

| 元素 | 规则 |
|---|---|
| 分隔符 | 空白分隔 token；`,` 在 `for` 列表中作分隔符，其余位置等价于空白 |
| 注释 | token 边界上的 `;` 或 `//` 起，到行尾（整行剩余部分忽略） |
| 字符串 | 双引号包围 `"..."`，内部 `\"` 转义；未闭合引号在求值时报错 |
| 数字 | 整数 `123`；小数 `1.5`；科学计数 `1e3`、`1.5e-7`（指数可带符号） |
| 标识符 | 字母或 `_` 开头，字母/数字/`_` 组成；**大小写敏感** |
| 变量 | `$` 前缀 + 标识符（仅 `RS_ENABLE_EXPR=1` 时有效） |
| 空行 / 纯注释行 | 忽略 |

## 3. 变量与表达式（宏开关，低端 MCU 不编译）

### 3.1 变量

- 命名：`$name`，区分大小写；作用域分**脚本级**（当前执行单元私有）。
- 类型：整数、浮点、布尔、字符串，由赋值的表达式类型决定，可重新赋值改变类型。
- 赋值语句（行首 token 为 `$`）：

```
$x = 10
$s = "hello"
$ok = $x > 5
```

### 3.2 表达式

支持字面量、变量与下列运算，优先级从高到低：

```
( )            括号
! - (一元)     逻辑非、取负
++ --          自增/自减（前缀 ++$x 与后缀 $x++，仅作用于整数/浮点变量）
* / %          乘、除、取余
+ -            加、减
< <= > >=      关系
== !=          相等
&& ||          逻辑与、或
```

- 整数与浮点混合运算时提升为浮点；除零结果为 `inf`，取余除零报错。
- 字符串仅参与 `==` / `!=` 与 `+` 拼接（任一操作数为字符串时结果为字符串）。
- 自增/自减：前缀先加减后取值，后缀先取值后加减；单独成行时等价于
  `$x = $x + 1` / `$x = $x - 1`；对非数值变量使用报 `type mismatch`。

示例：

```
$a = (3 + 4) * 2          ; $a = 14
$b = $a / 4               ; 整数相除截断为 3；若 $a = 14.0 则得 3.5
$flag = $a > 10 && $b != 3
$msg = "temp=" + 25.5     ; $msg = "temp=25.5"
$n = 0
$n++                      ; $n = 1
$m = $n++                 ; $m = 1，$n = 2（后缀先取值）
++$n                      ; $n = 3
```

### 3.3 内置函数与常量

表达式中可直接调用下列内置函数（宏 `RS_ENABLE_MATH`，默认跟随
`RS_ENABLE_EXPR`）。函数名**全小写、大小写敏感**；整数实参自动提升为
浮点参与运算，`abs` / `min` / `max` 在**全部实参为整数**时返回整数，其余一律
返回浮点。域错误交给 C 库处理（如 `sqrt(-1)` 得 `nan`），按现有浮点值语义传播。

| 函数 | 参数 | 说明 |
|---|---|---|
| `abs(x)` | 1 | 绝对值 |
| `floor(x)` | 1 | 向下取整 |
| `ceil(x)` | 1 | 向上取整 |
| `round(x)` | 1 | 四舍五入（.5 远离零方向，C99 `roundf`） |
| `sqrt(x)` | 1 | 平方根 |
| `sin(x)` `cos(x)` `tan(x)` | 1 | 三角函数（弧度） |
| `asin(x)` `acos(x)` `atan(x)` | 1 | 反三角函数（弧度） |
| `exp(x)` | 1 | e 的 x 次幂 |
| `log(x)` | 1 | 自然对数 |
| `log10(x)` | 1 | 常用对数 |
| `pow(x, y)` | 2 | x 的 y 次幂 |
| `atan2(y, x)` | 2 | 四象限反正切 |
| `min(x, y)` | 2 | 较小值 |
| `max(x, y)` | 2 | 较大值 |
| `tick()` | 0 | 内核 tick 毫秒计数（32 位回绕按有符号交付） |
| `uptime()` | 0 | 开机秒数 |
| `time()` | 0 | Unix 秒时间戳；RTC 未设置 / 不可用时回退开机秒数 |
| `date()` | 0 | `"YYYY-MM-DD"`；RTC 无效时由 `time()` 秒数换算 |
| `empty(x)` | 1 | 任意类型判空：空串 / false / 数值 0 时为 true |

| 常量 | 值 | 说明 |
|---|---|---|
| `PI` | 3.14159265… | 圆周率（浮点） |
| `E` | 2.71828182… | 自然对数底（浮点） |

```
$x = sqrt(pow(3, 2) + pow(4, 2))   ; $x = 5
$y = sin($t) * 100                 ; 可参与任意表达式
$z = PI * $r                       ; 常量以裸标识符引用
if empty($s)
    ...
end
```

- 调用时内置函数优先于扩展函数表、扩展函数表优先于用户函数表（§3.4）；
  与内置函数 / 内置常量同名的 `function` 定义在登记时报 `duplicate function`。
  实参个数不符报 `arg count mismatch`。math 函数非数值实参报 `type mismatch`；
  系统函数（`tick` / `uptime` / `time` / `date` / `empty`）自行处理实参类型，
  `empty` 接受任意类型。
- 常量以**裸标识符**引用（`PI`，不是 `$PI`），与变量是两个命名空间：
  裸标识符从不引用变量，`$x` 也从不引用常量。
- `time()` / `date()` 优先使用平台 RTC（`hal_rtc_get_timestamp` /
  `hal_rtc_get_time`）；RTC 未设置或不可用时 `time()` 回退为开机秒数，
  `date()` 按公历由该秒数换算（开机不足一天即得 `1970-01-01`）。
- 低端平台（如 `ROS_STM32_F1`，`RS_ENABLE_EXPR=0`）自动裁剪整个
  内置函数表，零代码成本；也可显式 `-D RS_ENABLE_MATH=0` 单独裁掉。

### 3.4 扩展注册（函数 / 常量）

平台 / 组件可向解释器注入全局函数与常量（`RS_ENABLE_EXPR=0` 时
注册返回 `RS_ERR_UNSUPPORTED`）：

```c
typedef rs_arg_t (*rs_func_fn_t)(const rs_arg_t *args,
                                           uint32_t nargs);
int rs_func_register(const char *name, uint8_t argc, rs_func_fn_t fn);
int rs_const_register(const char *name, double value);
```

- 扩展函数在脚本中以 `name(实参...)` 调用，不绑定实例；查找优先级：
  内置函数 → 扩展函数 → 脚本 `function`。
- 扩展常量以裸标识符引用，值为浮点；查找优先级：内置常量 → 扩展常量。
- 实参以 `rs_arg_t` 值传递：INT / FLOAT 原样传入，BOOL 以 0/1 整数
  传入，字符串实参在调用前即报 `type mismatch`；返回值仅接受
  `RS_ARG_INT` / `RS_ARG_FLOAT` / `RS_ARG_BOOL`（其余按
  `type mismatch` 处理）。
- `name` 需为标识符（字母 / 下划线开头，长度 < `RS_NAME_MAX`）；
  与内置函数、内置常量或已注册扩展名重名返回 `RS_ERR_PARAM`（-1）；
  表满返回 `RS_ERR_LIMIT`（容量 `RS_EXT_FUNC_MAX` /
  `RS_EXT_CONST_MAX`，默认各 8）。
- 注册表为进程级静态，不支持反注册；已注册名同样保留，`function`
  重定义会被拒绝。

## 4. 语句语法

### 4.1 条件

```
if <条件>
    ...
elseif <条件>
    ...
else
    ...
end
```

- `elseif` 可多次，`else` 至多一次且必须在 `elseif` 之后；`else`、`elseif` 均可省略。
- 条件必须是布尔表达式（`RS_ENABLE_EXPR=0` 时本语句不可用）。

示例：

```
if $bat < 20
    echo low battery
elseif $bat < 50
    echo battery half
else
    echo battery ok
end
```

### 4.2 for 循环（值列表）

```
for <表达式1>, <表达式2>, ... do
    ...
end
```

- 按列表逐个取值迭代，每个值执行一轮循环体。
- 可在列表前绑定循环变量：

```
for $i = 1, 2, 3 do
    ...
end
```

- 列表最多 16 项；`do` 与 `end` 必需。

示例：

```
for $pos = 100, 200, 300 do
    move_arm($pos)
end

for "a", "b", "c" do        ; 不需要当前值时可省略绑定
    beep()
end
```

### 4.3 while 循环

```
while <条件>
    ...
end
```

- 以 `end` 闭合。

示例（轮询等待，最多重试 10 次）：

```
$try = 0
while $try < 10
    $ready = sensor_ok()
    if $ready
        break
    end
    $try++
    wait 100
end
```

> `wait` 是内置命令（§9），在脚本中单独成行使用；低端裁剪掉表达式后
> `while`/`if` 不可用，轮询逻辑由上位机分条下发命令实现。

### 4.4 循环控制

```
break        ; 跳出最近一层 for/while
```

- `break` 出现在循环外会解析失败；嵌套时作用于**最近的**循环。
- 不支持 `continue`。

### 4.5 函数与调用

```
function <名称>($p1, $p2, ...)
    ...
    return [<表达式>]
end

<名称>(<实参列表>)          ; 隐式调用：在当前任务中同步执行，丢弃返回值
$x = <名称>(<实参列表>)     ; 调用并把返回值赋给变量（也可参与任意表达式）
```

- **没有 `call`/`run` 关键字**：行首为已登记函数名即构成调用语句，同步执行；
  异步执行使用内置命令 `run`（见 §9）。
- 形参为 `$name`，按位置**传值**（拷贝），函数体内形参即局部变量；实参为
  表达式，个数必须与形参一致（不匹配报 `arg count mismatch`）。
- `return` 提前结束函数；`return <表达式>` 携带返回值，赋值到具体变量或
  参与求值：`$x = f(1) + 2`；无返回值调用按 0 处理。
- 函数名大小写敏感、全脚本唯一（重复报 `duplicate function`），最多 32 个，
  每个最多 8 个参数。
- 调用允许前向引用：加载扫描仅登记函数名与起始行号，执行时按名解析；
  目标未定义报 `undefined function`。
- 调用嵌套上限 4 层（含 `run` 命令派生任务中的调用），超出报 `call stack
  overflow`；`RS_ENABLE_EXPR=0` 时函数仅支持无参定义与无参调用。

示例：

```
function clamp($v, $lo, $hi)
    if $v < $lo
        return $lo
    end
    if $v > $hi
        return $hi
    end
    return $v
end

$speed = clamp(150, 0, 100)      ; $speed = 100
blink(3)                         ; 隐式调用，丢弃返回值

function blink($n)               ; 前向调用合法：定义可在使用之后
    for $i = 1, $n do
        led_toggle()
        wait 200
    end
end
```

## 5. 块结构约束

- 块起始关键字：`if` `for` `while` `function`；统一以 `end` 闭合。
- 配对关系：`if…[elseif…]…[else]…end`、`for…do…end`、`while…end`、
  `function…end`。
- 嵌套深度上限 8；未闭合块报 `unclosed block`，错配报 `unmatched end`。
- 行首 token 不是已知关键字、也不是赋值语句/表达式语句时报 `unknown statement`。

## 6. 编译宏与低端裁剪

| 宏 | 默认 | =0 时移除 |
|---|---|---|
| `RS_ENABLE_EXPR` | 1 | 变量、赋值语句、表达式求值器；`if`/`while`/`for` 的列表求值不可用（解析期报 `unsupported`），函数退化为无参定义与无参隐式调用；内置命令 `wait`/`load`/`run` 不受影响（参数为字面量/变量替换） |
| `RS_ENABLE_MATH` | =EXPR | 内置 math 函数库（§3.3）；`EXPR=0` 时强制为 0 |

裁剪后预计节省：表达式求值器 + 词法变量分支 + 变量表 ≈ 4-6K Flash（参考现有 shell_expr 实现）。

## 7. 资源上限（编译期配置）

| 项 | 默认值 | 说明 |
|---|---|---|
| 单行字节数 | 128 | 行缓冲固定，栈上复用 |
| 单行 token 数 | 48 | 仅解释当前行时存在 |
| 脚本字节数 | 4096 | 源文本即唯一存储（≈4K RAM 峰值） |
| 块嵌套深度 | 8 | 块帧栈 8×16B |
| 函数数 | 32 | 加载扫描登记（名 + 行号） |
| 单函数参数数 | 8 | |
| 调用嵌套深度 | 4 | |
| for 列表项数 | 16 | |
| 变量数（宏开启时） | 16 | |
| 并发 `run` 命令派生任务数 | 16 | 每任务独立块帧栈与变量表 |

## 8. 语法速查（EBNF）

```
program    := { statement NEWLINE }
statement  := assignment
            | 'if' expr NEWLINE block
              { 'elseif' expr NEWLINE block }
              [ 'else' NEWLINE block ] 'end'
            | 'for' [ var '=' ] expr { ',' expr } 'do' NEWLINE block 'end'
            | 'while' expr NEWLINE block 'end'
            | 'break'
            | 'function' IDENT '(' [ var { ',' var } ] ')' NEWLINE block 'end'
            | 'return' [ expr ]
            | func_call                       ; 隐式调用语句（丢弃返回值）
            | command                         ; wait/load/run 等为内置命令，见 §9
assignment := var '=' expr | ( '++' | '--' ) var | var ( '++' | '--' )
expr       := literal | var | func_call | unary | inc_dec | binary | logical | comparison
func_call  := IDENT '(' [ expr { ',' expr } ] ')'
block      := { statement NEWLINE }
```

## 9. 内置命令

命令名同样**全小写、大小写敏感**。每行输入按三级分派：

1. 内置命令（下表）；
2. 全局命令表（cmd 组件注册的设备命令，现属 roshell，如 `gpio`、`pwm`）；
3. 脚本引擎（按 §4 语句执行，即交互模式）。

| 命令 | 用法 | 说明 |
|---|---|---|
| `help` | `help [cmd]` | 列出全部命令（按 basic/task/sys/run/session 分组）；带参数显示单条详情 |
| `ver` | `ver` | 固件版本与 roshell 配置（行/脚本/run 槽上限、HAS_FS） |
| `echo` | `echo <text\|$var>...` | 回显参数；`$var` 替换为变量值（需 `RS_ENABLE_EXPR=1`） |
| `ps` | `ps` | 任务列表 + run 槽位状态表 |
| `jobs` | `jobs` | 列出 `run` 命令派生的脚本任务（槽号、函数名、状态） |
| `run` | `run <fn> [args...]` | 异步执行：新建任务运行已登记函数，立即返回槽号；实参为字面量或 `$var`（命令层替换，不做表达式求值），个数须与形参一致 |
| `wait` | `wait <ms>` | 当前任务延时指定毫秒 |
| `load` | `load <path>` | 从文件系统读取脚本源码并**同步执行**；解析失败或文件不可读报告错误行号；`RS_HAS_FS=0` 时返回不支持 |
| `waitjob` | `waitjob <n> [timeout <ms>]` | 等待槽 n 结束，默认超时 30000ms；期间 Ctrl-C 可中断 |
| `kill` | `kill <name\|#tid>` | 挂起并回收任务（拒绝操作 `roshell_io` 自身） |
| `pause` | `pause <name\|#tid>` | 挂起任务 |
| `resume` | `resume <name\|#tid>` | 恢复任务 |
| `mem` | `mem` | 堆使用统计 |
| `state` | `state` | 内核状态（tick、任务数、调度统计） |
| `exit` | `exit` | 结束会话并回收 roshell 实例 |

- 命令参数按空白分词，仅支持字面量与 `$var` 替换，**不做表达式求值**
  （`wait 1000+500` 非法，`run f $x 3` 合法）。
- 内置命令名与函数命名空间冲突时命令优先：不允许定义与内置命令同名的函数
  （登记时报 `unknown function` 类错误）。

会话示例（`>` 为提示符）：

```
> $cal = 45
> echo 校准值 $cal 开始
校准值 45 开始
> load "home.rs"             ; 先加载脚本，登记 home_seq 函数
> run home_seq $cal 3        ; 异步执行函数 home_seq($a,$b)
job slot 0 started
> jobs
0  home_seq  running
> waitjob 0                  ; 阻塞至结束（Ctrl-C 可中断）
job 0 done
> exit
```

## 10. 使用说明

### 10.1 接入与启动

- 平台层调用 `rs_shell_start(version)` 创建会话任务 `roshell_io`
  （默认优先级 4、栈 3072B，宏 `RS_SHELL_PRIO/STACK` 可调）。
- 输入字节经 `rs_shell_feed(data, len)` 投喂（串口中断/轮询或 PC
  标准输入均可），内部组行后走 §9 三级分派。
- 输出使用 `ros_printf` / `ros_puts`，与内核日志共用通道。
- 生命周期查询：`rs_shell_started()`、`rs_shell_task_finished()`；
  `exit` 或 EOF 后由平台胶水调用 `rs_shell_shutdown()` 回收。

### 10.2 交互操作

- 提示符下逐行执行；输入块关键字（`if`/`for`/`while`/`function`）后自动
  进入续行模式，直到 `end` 配对后整块执行。
- 行编辑为简化实现：退格、Ctrl-C 清空当前行；无历史与 Tab 补全。
- 单行上限 128B（超长截断）；粘贴超过接收环形缓冲 256B
  （`RS_SHELL_RX_MAX`）会丢字节。

### 10.3 脚本执行

- 同步：`load <path>` 命令（需 `RS_HAS_FS=1`）或
  API `rs_exec(rs, text)`；脚本内函数用隐式调用语句同步执行。
- 异步：`run <fn> [args...]` 命令新建任务执行函数，槽位上限 16
  （`RS_RUN_MAX`，小内存平台建议收紧到 4）；用 `jobs` 查看、
  `waitjob <n>` 等待。函数表不跨脚本持久，`load` 的脚本结束即释放。
- 顶层变量表在会话内持久（跨行、跨 `load` 可见），便于交互调试。

### 10.4 低端平台裁剪

- `ROS_STM32_F1` 系列默认 `RS_ENABLE_EXPR=0`：无变量/表达式，
  `echo $var` 按原文输出，if/while/for/函数参数不可用；
  会话与内置命令、顺序执行脚本仍可用。
- 无文件系统平台加 `-D RS_HAS_FS=0` 裁掉 `load` 及 stdio 依赖。

## 11. 完整脚本示例

上位机通过串口一次性下发（或存为文件后 `load`）：

```
; selfcheck.rs - 开机自检
function beep_ok()
    buzzer(1)
    wait 100
    buzzer(0)
end

function check_sensor($name, $limit)
    $v = read_sensor($name)
    if $v >= 0 && $v < $limit
        return 1
    end
    echo fail $name $v
    return 0
end

$pass = 0
$total = 0

for $s = "imu", "lidar", "motor" do
    $total++
    $ok = check_sensor($s, 1000)
    if $ok
        $pass++
    end
end

if $pass == $total
    echo selfcheck pass $pass / $total
    beep_ok()
else
    echo selfcheck fail $pass / $total
end

; 后台启动巡检任务，不阻塞会话收尾
run patrol 60
```

要点：`;` 注释、函数带参与返回值、`for` 字符串列表迭代、`wait`/`echo`/`run`
命令在脚本行内使用、错误信息带行号（如 `[12] ...`）。
