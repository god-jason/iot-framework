# MotionScript V1.0 语法说明

语言名称： MotionScript\
文件扩展名： `.ms`\
运行平台： STM32、ARM Linux\
执行方式： 纯文本，逐行解析，逐行执行\
设计目标： 控制步进电机、直流电机、风机、PWM、GPIO、ADC、传感器等执行器，并支持流程控制、任务调度和异常处理。

# 1. 基本语法规范

## 1.1 指令格式

每条指令独占一行，基本格式：

```
COMMAND [DEVICE_ID] ACTION [, PARAM VALUE]...
```

示例：

```
STEP 0 START, DIR 1, SPEED 1000rpm
PWM 0 START, FREQ 1000Hz, DUTY 50%
GPIO 1 ON
WAIT TIME 10s
```

## 1.2 大小写

关键字不区分大小写。

```
STEP 0 START, DIR 1, SPEED 1000rpm
step 0 start, dir 1, speed 1000rpm
Step 0 Start, Dir 1, Speed 1000rpm
```

建议实际脚本统一使用大写关键字。

## 1.3 注释

支持两种注释：

```
; 这是注释

// 这也是注释

STEP 0 START, DIR 1, SPEED 1000rpm ; 启动电机
```

注释内容不会被执行。

## 1.4 空行

允许空行，解释器忽略空行。

## 1.5 指令分隔符

参数之间使用逗号分隔。

```
STEP 0 START, DIR 1, SPEED 1000rpm, TIME 1000ms
```

不允许一行写多条指令。

# 2. 数据类型和单位

## 2.1 数据类型

支持以下数据类型：

| 类型       | 示例       | 说明  |
| -------- | -------- | --- |
| INTEGER  | `100`    | 整数  |
| FLOAT    | `12.5`   | 浮点数 |
| BOOLEAN  | `TRUE`   | 布尔值 |
| STRING   | `"FEED"` | 字符串 |
| VARIABLE | `$SPEED` | 变量  |

布尔值支持：

```
TRUE
FALSE
1
0
ON
OFF
```

## 2.2 时间单位

| 单位  | 说明 |
| --- | -- |
| ms  | 毫秒 |
| s   | 秒  |
| min | 分钟 |
| h   | 小时 |

示例：

```
WAIT TIME 100ms
WAIT TIME 10s
WAIT TIME 2min
```

## 2.3 速度单位

| 单位     | 说明    |
| ------ | ----- |
| rpm    | 每分钟转数 |
| rps    | 每秒转数  |
| step/s | 每秒脉冲数 |
| mm/s   | 毫米每秒  |
| deg/s  | 角度每秒  |

## 2.4 频率单位

```
Hz
kHz
```

## 2.5 百分比单位

```
%
```

## 2.6 角度和距离单位

```
deg
mm
cm
m
```

## 2.7 默认单位

允许省略单位，但每种设备参数必须规定默认单位。

例如：

```
STEP 0 START, SPEED 1000
PWM 0 START, FREQ 1000, DUTY 50
WAIT TIME 1000
```

建议默认单位：

| 参数         | 默认单位   |
| ---------- | ------ |
| STEP SPEED | step/s |
| PWM FREQ   | Hz     |
| PWM DUTY   | %      |
| WAIT TIME  | ms     |
| STEP TIME  | ms     |
| ACC / DEC  | ms     |

# 3. 变量

## 3.1 变量定义

使用 `SET` 定义和修改变量。

```
SET $SPEED = 1000
SET $COUNT = 10
SET $WEIGHT = 100.5
SET $ENABLE = TRUE
SET $NAME = "FEED"
```

变量名以 `$` 开头。

## 3.2 变量赋值

```
SET $SPEED = 1000
SET $SPEED = $SPEED + 100
SET $COUNT = $COUNT - 1
SET $WEIGHT = $WEIGHT * 2
SET $VALUE = $WEIGHT / 10
```

支持运算符：

| 运算符 | 说明 |
| --- | -- |
| `+` | 加法 |
| `-` | 减法 |
| `*` | 乘法 |
| `/` | 除法 |
| `%` | 取余 |

## 3.3 变量使用

```
STEP 0 START, DIR 1, SPEED $SPEED
WAIT TIME $TIME
PWM 0 START, DUTY $DUTY
```

## 3.4 变量作用域

V1.0 建议所有变量属于当前 JOB，JOB 之间相互独立。

如果需要共享变量，后续版本再增加全局变量机制。

# 4. STEP 步进电机指令

## 4.1 启动电机

语法：

```
STEP ID START [, PARAM VALUE]...
```

参数：

| 参数    | 类型      | 说明       |
| ----- | ------- | -------- |
| DIR   | INTEGER | 方向，0 或 1 |
| SPEED | NUMBER  | 运行速度     |
| TIME  | TIME    | 运行时间     |
| PULSE | INTEGER | 运行脉冲数    |
| ACC   | TIME    | 加速时间     |
| DEC   | TIME    | 减速时间     |

示例：

```
STEP 0 START, DIR 1, SPEED 1000rpm
STEP 0 START, DIR 0, SPEED 500rpm, TIME 1000ms
STEP 0 START, DIR 1, SPEED 1000rpm, PULSE 3200
STEP 0 START, DIR 1, SPEED 1000rpm, ACC 1s, DEC 1s
```

说明：

- START 默认非阻塞。
- TIME 和 PULSE 不能同时使用。
- 不指定 TIME 和 PULSE 时，持续运行直到 STOP。
- ACC、DEC 由电机组件执行。

## 4.2 停止电机

```
STEP ID STOP
STEP ID STOP, DEC TIME
```

示例：

```
STEP 0 STOP
STEP 0 STOP, DEC 1s
```

## 4.3 设置速度

```
STEP ID SPEED VALUE
```

示例：

```
STEP 0 SPEED 1000rpm
```

## 4.4 设置方向

```
STEP ID DIR VALUE
```

示例：

```
STEP 0 DIR 1
```

## 4.5 电机使能

```
STEP ID ENABLE
STEP ID DISABLE
```

## 4.6 电机回零

```
STEP ID HOME [, PARAM VALUE]...
```

参数：

| 参数      | 说明   |
| ------- | ---- |
| SPEED   | 回零速度 |
| DIR     | 回零方向 |
| TIMEOUT | 超时时间 |

示例：

```
STEP 0 HOME, SPEED 100rpm, TIMEOUT 30s
```

## 4.7 位置控制

```
STEP ID ZERO
STEP ID POSITION READ
STEP ID POSITION SET VALUE
```

示例：

```
STEP 0 ZERO
STEP 0 POSITION READ
STEP 0 POSITION SET 0
```

# 5. PWM 指令

## 5.1 启动 PWM

```
PWM ID START [, PARAM VALUE]...
```

参数：

| 参数    | 类型     | 说明      |
| ----- | ------ | ------- |
| FREQ  | NUMBER | PWM 频率  |
| DUTY  | NUMBER | 占空比     |
| ACCEL | TIME   | 占空比渐变时间 |

示例：

```
PWM 0 START, FREQ 1000Hz, DUTY 50%
PWM 0 START, FREQ 10000Hz, DUTY 80%, ACCEL 1s
```

## 5.2 设置占空比

```
PWM ID DUTY VALUE
PWM ID DUTY VALUE, ACCEL TIME
```

示例：

```
PWM 0 DUTY 30%
PWM 0 DUTY 80%, ACCEL 2s
```

## 5.3 设置频率

```
PWM ID FREQ VALUE
```

示例：

```
PWM 0 FREQ 10000Hz
```

## 5.4 停止 PWM

```
PWM ID STOP
PWM ID STOP, DECEL TIME
```

示例：

```
PWM 0 STOP
PWM 0 STOP, DECEL 2s
```

# 6. GPIO 指令

## 6.1 输出控制

```
GPIO ID ON
GPIO ID OFF
GPIO ID SET, VALUE 1
GPIO ID SET, VALUE 0
```

## 6.2 输入读取

```
GPIO ID READ
GPIO ID READ, TO $VALUE
```

示例：

```
GPIO 0 ON
GPIO 1 OFF
GPIO 2 READ, TO $STATE
```

# 7. ADC 指令

## 7.1 ADC 读取

```
ADC ID READ
ADC ID READ, TO $VALUE
ADC ID READ, AVG COUNT, TO $VALUE
```

示例：

```
ADC 0 READ, TO $VALUE
ADC 1 READ, AVG 10, TO $VALUE
```

## 7.2 ADC 条件等待

```
ADC ID WAIT, GT VALUE
ADC ID WAIT, LT VALUE
```

示例：

```
ADC 0 WAIT, GT 2000
ADC 0 WAIT, LT 1000
```

# 8. 传感器指令

## 8.1 读取传感器

```
SENSOR ID READ
SENSOR ID READ, TO $VALUE
```

## 8.2 等待传感器状态

```
SENSOR ID WAIT, VALUE 1
SENSOR ID WAIT, VALUE 0, TIMEOUT 10s
```

# 9. RELAY 指令

```
RELAY ID ON
RELAY ID OFF
RELAY ID READ
```

示例：

```
RELAY 0 ON
RELAY 1 OFF
```

# 10. WAIT 等待指令

WAIT 用于等待时间、设备动作完成或指定条件满足。

## 10.1 时间等待

```
WAIT TIME VALUE
```

示例：

```
WAIT TIME 100ms
WAIT TIME 10s
WAIT TIME 2min
```

## 10.2 等待电机

```
WAIT STEP ID
WAIT STEP ID, TIMEOUT TIME
```

示例：

```
WAIT STEP 0
WAIT STEP 0, TIMEOUT 30s
```

## 10.3 等待 PWM

```
WAIT PWM ID
WAIT PWM ID, TIMEOUT TIME
```

## 10.4 等待 GPIO

```
WAIT GPIO ID, VALUE VALUE
WAIT GPIO ID, VALUE VALUE, TIMEOUT TIME
```

## 10.5 等待传感器

```
WAIT SENSOR ID, VALUE VALUE
WAIT SENSOR ID, VALUE VALUE, TIMEOUT TIME
```

## 10.6 等待全部动作

```
WAIT ALL
```

WAIT 遇到条件未满足时，暂停当前脚本执行，满足条件后继续。

# 11. LABEL / GOTO 指令

## 11.1 定义标签

```
LABEL NAME
```

## 11.2 跳转标签

```
GOTO NAME
```

示例：

```
LABEL START

STEP 0 START, DIR 1, SPEED 1000rpm, TIME 1000ms
WAIT STEP 0

GOTO START
```

标签名称在同一个 JOB 中必须唯一。

# 12. IF / ELSE / END 条件指令

语法：

```
IF CONDITION
    COMMAND
ELSE
    COMMAND
END
```

支持比较运算符：

| 运算符  | 说明   |
| ---- | ---- |
| `==` | 等于   |
| `!=` | 不等于  |
| `>`  | 大于   |
| `>=` | 大于等于 |
| `<`  | 小于   |
| `<=` | 小于等于 |

支持逻辑运算符：

```
AND
OR
NOT
```

示例：

```
IF $VALUE > 100
    GPIO 0 ON
ELSE
    GPIO 0 OFF
END
```

多条件示例：

```
IF $WEIGHT > 100 AND $STATE == 1
    GPIO 0 ON
END
```

允许 IF 嵌套，但必须正确匹配 END。

# 13. LOOP / ENDLOOP 循环指令

## 13.1 固定次数循环

```
LOOP COUNT
    COMMAND
ENDLOOP
```

示例：

```
LOOP 10
    STEP 0 START, DIR 1, SPEED 1000rpm, PULSE 3200
    WAIT STEP 0
ENDLOOP
```

## 13.2 变量次数循环

```
LOOP $COUNT
    GPIO 0 ON
    WAIT TIME 1s
    GPIO 0 OFF
ENDLOOP
```

## 13.3 无限循环

```
LOOP
    GPIO 0 ON
    WAIT TIME 1s
    GPIO 0 OFF
    WAIT TIME 1s
ENDLOOP
```

## 13.4 BREAK

```
LOOP 100
    ADC 0 READ, TO $VALUE

    IF $VALUE > 3000
        BREAK
    END

    WAIT TIME 100ms
ENDLOOP
```

# 14. CALL / RETURN 子程序指令

## 14.1 调用子程序

```
CALL NAME
```

## 14.2 返回

```
RETURN
```

示例：

```
CALL HOME
CALL FEED

ENDJOB

LABEL HOME
    STEP 0 HOME, SPEED 100rpm
    WAIT STEP 0
RETURN

LABEL FEED
    STEP 0 START, DIR 1, SPEED 1000rpm, TIME 10s
    WAIT STEP 0
RETURN
```

CALL 调用子程序，RETURN 返回调用位置。

# 15. ERROR 异常指令

## 15.1 主动触发异常

```
ERROR CODE
ERROR "MESSAGE"
```

示例：

```
ERROR 1001
ERROR "WEIGHT ERROR"
```

## 15.2 异常处理入口

```
ON ERROR
    COMMAND
    COMMAND
ENDERROR
```

示例：

```
ON ERROR
    STEP 0 STOP
    PWM 0 STOP
    GPIO 0 OFF
ENDERROR
```

建议预定义异常：

| 错误码  | 说明     |
| ---- | ------ |
| 1001 | 参数错误   |
| 1002 | 设备不存在  |
| 1003 | 设备执行失败 |
| 1004 | 等待超时   |
| 1005 | 传感器错误  |
| 1006 | 电机错误   |
| 1007 | 限位触发   |
| 1008 | 程序执行错误 |

# 16. JOB 任务指令

## 16.1 定义任务

```
JOB NAME
    COMMAND
ENDJOB
```

示例：

```
JOB FEED
    STEP 0 START, DIR 1, SPEED 1000rpm, TIME 10s
    WAIT STEP 0
ENDJOB
```

## 16.2 启动任务

```
START JOB NAME
```

## 16.3 停止任务

```
STOP JOB NAME
```

## 16.4 等待任务

```
WAIT JOB NAME
```

## 16.5 任务状态

```
JOB NAME STATE, TO $STATE
```

建议 JOB 支持：

```
IDLE
RUNNING
WAITING
PAUSED
COMPLETED
ERROR
ABORTED
```

# 17. 完整示例

```
; =====================================
; 自动投料任务
; =====================================

JOB FEED

    SET $TARGET_WEIGHT = 1000
    SET $CURRENT_WEIGHT = 0
    SET $SPEED = 1000

    ; 回原点
    CALL HOME

    ; 启动风机
    PWM 0 START, FREQ 10000Hz, DUTY 50%, ACCEL 1s

    ; 启动振动器
    GPIO 0 ON

    ; 开始投料
    STEP 0 START, DIR 1, SPEED $SPEED

LABEL FEED_LOOP

    ADC 0 READ, AVG 10, TO $CURRENT_WEIGHT

    IF $CURRENT_WEIGHT >= $TARGET_WEIGHT
        GOTO FEED_FINISH
    END

    WAIT TIME 100ms
    GOTO FEED_LOOP

LABEL FEED_FINISH

    STEP 0 STOP, DEC 500ms
    WAIT STEP 0

    GPIO 0 OFF

    PWM 0 STOP, DECEL 2s
    WAIT PWM 0

    CALL HOME

ENDJOB


LABEL HOME
    STEP 0 HOME, SPEED 100rpm, TIMEOUT 30s
    WAIT STEP 0
RETURN
```

# 18. 语法和执行约束

| 项目         | V1.0 约定               |
| ---------- | --------------------- |
| 指令格式       | 一行一条                  |
| 编码         | UTF-8，建议脚本关键字使用 ASCII |
| 最大行长度      | 256 字节                |
| 最大变量数      | 128                   |
| 最大 CALL 深度 | 8                     |
| 最大 LOOP 嵌套 | 8                     |
| 最大 IF 嵌套   | 8                     |
| 最大 JOB 数量  | 16                    |
| 最大标签数量     | 128                   |
| 动态内存       | 不使用                   |
| 递归         | 不支持                   |
| 浮点运算       | 可选，建议定点数实现            |
| 指令执行       | 逐行解析                  |
| 运动执行       | 非阻塞                   |
| 等待执行       | 状态机实现                 |
| 错误处理       | 错误码 + ON ERROR        |

几个需要在正式实现前固定的规则：

1. `END` 只用于结束 IF，`ENDLOOP` 结束循环，`ENDJOB` 结束任务，`ENDERROR` 结束异常处理。
2. `LABEL/GOTO` 和 `CALL/RETURN` 共用标签表，但 CALL 必须使用独立调用栈。
3. 每个 JOB 有自己的 PC、变量和流程栈；设备资源是否允许多个 JOB 同时控制，由资源管理器决定。
4. `START` 默认非阻塞，`WAIT` 负责同步。
5. `STOP` 是正常停止，`ABORT` 是立即终止脚本；真正的硬件急停由独立安全链路执行。

这份规范可以作为后续 `motion_parser.c`、`motion_vm.c` 和 `motion_executor.c` 的实现依据。