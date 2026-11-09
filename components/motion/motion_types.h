#ifndef MOTION_TYPES_H
#define MOTION_TYPES_H

/*
 * motion_types.h —— MotionScript 解释器的公共数据类型定义。
 *
 * 职责：集中定义解释器各模块共用的状态机、指令枚举、数值/单位模型、
 *       解析后的指令结构、等待条件与错误信息结构。
 *
 * 数据流：motion_parser 把文本行解析成 motion_instruction_t；
 *         motion_variable 负责 motion_value_t 的存储与求值；
 *         执行层消费 motion_instruction_t / motion_wait_t 并产生
 *         motion_state_t、motion_exec_result_e 与 motion_error_t。
 *
 * 边界约束：本头文件不涉及动态内存，所有数组容量均由
 *           motion_config.h 中的 MOTION_MAX_* 编译期常量确定。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "motion_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 解释器 / JOB 运行状态 */
typedef enum {
    MOTION_STATE_IDLE = 0,  /* 空闲：未加载程序或已复位 */
    MOTION_STATE_RUNNING,   /* 运行中：正在顺序执行指令 */
    MOTION_STATE_WAITING,   /* 等待中：阻塞在 WAIT 条件上，由轮询解除 */
    MOTION_STATE_PAUSED,    /* 暂停：保留执行现场，可恢复运行 */
    MOTION_STATE_COMPLETED, /* 完成：已执行到程序末尾 */
    MOTION_STATE_ERROR,     /* 出错：可跳转至 ON ERROR 处理块 */
    MOTION_STATE_ABORTED    /* 中止：被外部终止，不再继续执行 */
} motion_state_e;

/* 指令类型，覆盖 §4 - §16 全部语法 */
typedef enum {
    MOTION_CMD_NONE = 0,     /* 未识别 / 空指令 */

    MOTION_CMD_STEP,         /* STEP：步进电机指令 */
    MOTION_CMD_PWM,          /* PWM：脉宽调制输出 */
    MOTION_CMD_GPIO,         /* GPIO：通用数字量输入输出 */
    MOTION_CMD_ADC,          /* ADC：模拟量采集 */
    MOTION_CMD_SENSOR,       /* SENSOR：传感器读取 */
    MOTION_CMD_RELAY,        /* RELAY：继电器控制 */

    MOTION_CMD_WAIT,         /* WAIT：等待时间 / 设备状态 */
    MOTION_CMD_SET,          /* SET：变量赋值 */

    MOTION_CMD_IF,           /* IF：条件分支开始 */
    MOTION_CMD_ELSE,         /* ELSE：条件分支的另一支 */
    MOTION_CMD_END,          /* END：结束 IF 块 */
    MOTION_CMD_LOOP,         /* LOOP：循环块开始 */
    MOTION_CMD_ENDLOOP,      /* ENDLOOP：循环块结束 */
    MOTION_CMD_BREAK,        /* BREAK：提前跳出当前循环 */

    MOTION_CMD_LABEL,        /* LABEL：定义标签 */
    MOTION_CMD_GOTO,         /* GOTO：无条件跳转到标签 */
    MOTION_CMD_CALL,         /* CALL：调用子流程 */
    MOTION_CMD_RETURN,       /* RETURN：从子流程返回 */

    MOTION_CMD_ERROR,        /* ERROR：主动抛出错误 */
    MOTION_CMD_ON_ERROR,     /* ON ERROR：错误处理块开始 */
    MOTION_CMD_ENDERROR,     /* ENDERROR：错误处理块结束 */

    MOTION_CMD_JOB,          /* JOB：定义并启动并行任务 */
    MOTION_CMD_ENDJOB,       /* ENDJOB：并行任务定义结束 */
    MOTION_CMD_START_JOB,    /* START JOB：启动指定任务 */
    MOTION_CMD_STOP_JOB,     /* STOP JOB：停止指定任务 */
    MOTION_CMD_JOB_STATE     /* JOB ... STATE：查询任务状态 */
} motion_cmd_e;

/* 解析结果 */
typedef enum {
    MOTION_PARSE_OK = 0,     /* 解析成功 */
    MOTION_PARSE_EMPTY,      /* 空行或整行被注释 */
    MOTION_PARSE_INVALID,    /* 语法错误 */
    MOTION_PARSE_OVERFLOW,   /* 行过长或参数个数超限 */
    MOTION_PARSE_UNSUPPORTED /* 无法识别的指令关键字 */
} motion_parse_result_e;

/* 执行结果 */
typedef enum {
    MOTION_EXEC_OK = 0,      /* 本步执行完成，可继续下一条 */
    MOTION_EXEC_WAIT,        /* 需要等待，稍后从同一指令继续 */
    MOTION_EXEC_ERROR,       /* 执行出错 */
    MOTION_EXEC_ABORT,       /* 被中止 */
    MOTION_EXEC_COMPLETE     /* 程序执行完毕 */
} motion_exec_result_e;

/* 数值类型 */
typedef enum {
    MOTION_VALUE_NONE = 0,   /* 未初始化 / 空值 */
    MOTION_VALUE_INTEGER,    /* 64 位有符号整数 */
    MOTION_VALUE_FLOAT,      /* 双精度浮点数 */
    MOTION_VALUE_BOOLEAN,    /* 布尔值：TRUE/FALSE/ON/OFF */
    MOTION_VALUE_STRING,     /* 双引号字符串（定长缓冲） */
    MOTION_VALUE_VARIABLE    /* $变量引用，使用时需要查表求值 */
} motion_value_type_e;

/* 单位 */
typedef enum {
    MOTION_UNIT_NONE = 0,    /* 无单位 */
    MOTION_UNIT_MS,          /* 毫秒（s/min/h 解析时已换算为 ms） */
    MOTION_UNIT_HZ,          /* 赫兹（kHz 解析时已换算为 Hz） */
    MOTION_UNIT_PERCENT,     /* 百分比 0~100 */
    MOTION_UNIT_RPM,         /* 每分钟转数 */
    MOTION_UNIT_RPS,         /* 每秒转数 */
    MOTION_UNIT_STEP_S,      /* 每秒脉冲数 */
    MOTION_UNIT_MM_S,        /* 毫米每秒 */
    MOTION_UNIT_DEG_S,       /* 度每秒 */
    MOTION_UNIT_DEG,         /* 角度（度） */
    MOTION_UNIT_MM,          /* 毫米（cm/m 解析时已换算为 mm） */
    MOTION_UNIT_CM,          /* 厘米：保留值，解析阶段会转为 MM */
    MOTION_UNIT_M            /* 米：保留值，解析阶段会转为 MM */
} motion_unit_e;

/* 通用数值 / 变量引用 */
typedef struct {
    motion_value_type_e type; /* 当前联合体生效的成员，决定 data 的解释方式 */
    motion_unit_e unit;       /* 数值单位；字符串/布尔通常为 NONE */
    union {
        int64_t integer;                      /* type = INTEGER */
        double real;                          /* type = FLOAT */
        bool boolean;                         /* type = BOOLEAN */
        char string[MOTION_MAX_STRING];       /* type = STRING，含结尾 '\0' */
        char variable[MOTION_MAX_VAR_NAME];   /* type = VARIABLE，已去 '$' 并转大写 */
    } data;
} motion_value_t;

/* 指令参数：键值对形式（如 SPEED 1000rpm） */
typedef struct {
    char name[16];       /* 参数名，解析时统一转为大写 */
    motion_value_t value;/* 参数值（可能仍是 $变量引用，需执行期求值） */
} motion_param_t;

/* 解析后的一条指令 */
typedef struct {
    motion_cmd_e command;  /* 指令类型 */
    uint16_t device_id;    /* 目标设备号；非设备指令时为 0 */
    char action[16];       /* 动作名（大写），如 START/STOP/POSITION */
    char subaction[16];    /* 子动作名，如 STEP POSITION SET 中的 SET */
    char name[MOTION_MAX_LABEL_NAME]; /* 标签名 / JOB 名 */

    uint8_t param_count;                      /* 已写入 params 的参数个数 */
    motion_param_t params[MOTION_MAX_PARAMS]; /* 参数表，最多 MOTION_MAX_PARAMS 项 */

    uint32_t line;        /* 该指令在源文件中的行号（从 1 开始） */
    uint32_t offset;      /* 该指令在源中的起始字节偏移 */
    uint32_t next_offset; /* 下一条指令的起始偏移，用于执行跳转 */

    /* SET / IF / LOOP 的表达式原文 */
    char expression[MOTION_MAX_LINE];
} motion_instruction_t;

/* 比较条件 */
typedef enum {
    MOTION_CMP_NONE = 0, /* 未指定比较 */
    MOTION_CMP_EQ,       /* == */
    MOTION_CMP_NE,       /* != */
    MOTION_CMP_GT,       /* >  */
    MOTION_CMP_GE,       /* >= */
    MOTION_CMP_LT,       /* <  */
    MOTION_CMP_LE        /* <= */
} motion_compare_e;

/* 等待类型 */
typedef enum {
    MOTION_WAIT_NONE = 0, /* 未等待 */
    MOTION_WAIT_TIME,     /* 等待固定时长 */
    MOTION_WAIT_STEP,     /* 等待步进电机停止 */
    MOTION_WAIT_PWM,      /* 等待 PWM 停止 */
    MOTION_WAIT_GPIO,     /* 等待 GPIO 达到目标电平 */
    MOTION_WAIT_ADC,      /* 等待 ADC 满足比较条件 */
    MOTION_WAIT_SENSOR,   /* 等待传感器满足比较条件 */
    MOTION_WAIT_ALL,      /* 等待所有已启动设备空闲 */
    MOTION_WAIT_JOB       /* 等待指定 JOB 结束 */
} motion_wait_type_e;

/* 一条进行中的等待条件 */
typedef struct {
    motion_wait_type_e type; /* 等待类型 */
    bool active;             /* 是否处于等待中；false 表示该结构未被占用 */

    uint16_t device_id;      /* 被等待的设备号 */
    motion_compare_e compare;/* 目标值比较方式（设备/传感器等待使用） */
    int32_t target;          /* 目标比较值 */

    uint32_t start_tick;     /* 等待起始时刻（毫秒 tick），用于计算已等待时间 */
    uint32_t duration_ms;    /* WAIT TIME 使用：需等待的总时长 */
    uint32_t timeout_ms;     /* 设备等待超时，0 表示无限 */
    bool has_timeout;        /* 是否显式设置了 TIMEOUT */

    char job_name[MOTION_MAX_JOB_NAME]; /* WAIT JOB 使用：目标任务名 */
} motion_wait_t;

/* 错误信息 */
typedef struct {
    int code;                        /* 错误码，取值见 motion_error_code_e */
    uint32_t line;                   /* 出错行号；0 表示与具体行无关 */
    char message[MOTION_MAX_ERROR_MSG]; /* 人类可读的错误描述，定长且以 '\0' 结尾 */
} motion_error_t;

#ifdef __cplusplus
}
#endif

#endif /* MOTION_TYPES_H */
