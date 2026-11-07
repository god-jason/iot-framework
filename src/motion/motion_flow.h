#ifndef MOTION_FLOW_H
#define MOTION_FLOW_H

/*
 * 流程索引与表达式求值模块（MotionScript 解释器的加载期预处理层）。
 *
 * 职责：脚本加载时单遍扫描全文，把 IF/ELSE/END、LOOP/ENDLOOP、LABEL、
 * JOB/ENDJOB、ON ERROR/ENDERROR 等需要配对或回跳的结构，预解析为
 * 以字节偏移为键的静态索引表；运行期只需查表即可跳转，无需重扫源码。
 * 另提供递归下降表达式求值器，供条件判断与赋值使用。
 *
 * 执行模型：全部索引保存在本模块的静态数组中，不使用动态内存，
 * 容量由 motion_config.h 的 MOTION_MAX_* 宏限定。
 * motion_flow_build() 会先清空全部表，因此同一时刻只保留一份脚本索引。
 *
 * 依赖：motion_parser（逐行解析）、motion_source（读取文本）、
 *       motion_variable（表达式中的变量取值）。
 */

#include "motion_types.h"
#include "motion_variable.h"

#ifdef __cplusplus
extern "C" {
#endif

/* IF 块索引：记录 IF 指令及其 ELSE/END 的位置与跳转落点 */
typedef struct {
    uint32_t if_offset;      /* IF 指令自身的字节偏移 */
    uint32_t else_offset;    /* ELSE 指令偏移；无 ELSE 时为 0 */
    uint32_t else_next;      /* ELSE 下一行偏移（ELSE 分支体起点） */
    uint32_t end_offset;     /* 配对 END 的字节偏移 */
    uint32_t end_next;       /* END 下一行偏移（整个 IF 之后的续点） */
    int segment;             /* 所属段：0 主程序，>0 为 JOB 段号 */
    bool has_else;           /* 是否存在 ELSE 分支 */
} motion_if_entry_t;

/* LOOP 块索引：记录 LOOP/ENDLOOP 位置，循环体自 body_offset 开始 */
typedef struct {
    uint32_t loop_offset;    /* LOOP 指令自身的字节偏移 */
    uint32_t body_offset;    /* 循环体首行偏移（LOOP 下一行） */
    uint32_t endloop_offset; /* 配对 ENDLOOP 的字节偏移 */
    uint32_t endloop_next;   /* ENDLOOP 下一行偏移（循环结束后的续点） */
    int segment;             /* 所属段 */
} motion_loop_entry_t;

/* 标签索引：GOTO / CALL 据此解析目标地址 */
typedef struct {
    char name[MOTION_MAX_LABEL_NAME]; /* 标签名（不含冒号） */
    uint32_t offset;         /* 标签行自身的字节偏移 */
    uint32_t next_offset;    /* 标签下一行偏移，也是实际跳转落点 */
    uint32_t line;           /* 标签所在源码行号，从 1 开始 */
    int segment;             /* 所属段，0 为主程序 */
} motion_label_t;

/* JOB 块索引：每个 JOB 是一个可独立启停的子程序段 */
typedef struct {
    char name[MOTION_MAX_JOB_NAME]; /* JOB 名称 */
    uint32_t job_offset;     /* JOB 指令自身的字节偏移 */
    uint32_t start_offset;   /* JOB 体首行偏移（JOB 下一行） */
    uint32_t end_offset;     /* 配对 ENDJOB 的字节偏移 */
    uint32_t next_offset;    /* ENDJOB 下一行偏移 */
    int segment;             /* 该 JOB 运行时的段号（JOB 序号 + 1） */
} motion_job_entry_t;

/* ON ERROR 处理器索引：按段记录异常处理入口 */
typedef struct {
    bool active;              /* 该段是否定义了 ON ERROR */
    uint32_t on_offset;       /* ON ERROR 指令自身偏移 */
    uint32_t handler_offset;  /* 处理块首行偏移（ON ERROR 下一行） */
    uint32_t enderror_offset; /* 配对 ENDERROR 的字节偏移 */
    uint32_t enderror_next;   /* ENDERROR 下一行偏移（处理完成后的续点） */
} motion_error_entry_t;

/*
 * 加载期建立流程索引。
 * 参数：text 以 '\0' 结尾的脚本；NULL 视为空脚本。
 * 返回：0 成功；MOTION_ERR_FLOW 块不配对；MOTION_ERR_MEMORY 表满。
 * 注意：调用后会覆盖此前建立的全部索引。
 */
int motion_flow_build(const char *text);

/* 清空全部流程索引（计数与表内容），不释放任何外部资源 */
void motion_flow_reset(void);

/*
 * 查找标签。
 * 参数：name 标签名；segment 当前段（0 主程序 / >0 JOB 段）。
 * 返回：命中返回只读表项指针，否则 NULL。
 * 注意：本段未命中时，JOB 段会回退到主程序段查找（支持调用全局标签）。
 */
const motion_label_t *motion_flow_find_label(const char *name, int segment);

/* 按 IF 指令的字节偏移查找 IF 块索引，未命中返回 NULL */
const motion_if_entry_t *motion_flow_find_if(uint32_t offset);

/* 按 ELSE 指令的字节偏移查找所属 IF 块索引，未命中返回 NULL */
const motion_if_entry_t *motion_flow_find_else(uint32_t offset);

/* 按 LOOP 指令的字节偏移查找 LOOP 块索引，未命中返回 NULL */
const motion_loop_entry_t *motion_flow_find_loop(uint32_t offset);

/* 按 ON ERROR 指令的字节偏移查找处理器索引，未命中返回 NULL */
const motion_error_entry_t *motion_flow_find_error(uint32_t offset);

/*
 * 查找指定段的异常处理器。
 * 参数：segment 段号（0 主程序 / >0 JOB 段）。
 * 返回：命中返回表项指针，否则 NULL。
 * 注意：JOB 段自身无处理器时会回退使用主程序段（段 0）的处理器。
 */
const motion_error_entry_t *motion_flow_find_handler(int segment);

/* 按名称查找 JOB 索引，未命中返回 NULL */
const motion_job_entry_t *motion_flow_find_job(const char *name);

/* 按 JOB 指令的字节偏移查找 JOB 索引，未命中返回 NULL */
const motion_job_entry_t *motion_flow_find_job_by_offset(uint32_t offset);

/* 返回已登记的 JOB 总数（不会超过 MOTION_MAX_JOBS） */
uint8_t motion_flow_job_count(void);

/*
 * 按序号取 JOB 索引项。
 * 参数：index 序号，范围 [0, motion_flow_job_count())。
 * 返回：合法时返回只读指针，越界返回 NULL。
 */
const motion_job_entry_t *motion_flow_job_at(uint8_t index);

/*
 * 由字节偏移反查源码行号。
 * 返回：行号（从 1 开始）；偏移未命中任何已记录行首时返回 0。
 */
uint32_t motion_flow_line_at(uint32_t offset);

/*
 * 表达式求值（递归下降）。
 * 参数：
 *   expression - 以 '\0' 结尾的表达式文本；
 *   variables  - 变量表，用于解析表达式中的 $变量（可为 NULL）；
 *   result     - 输出求值结果。
 * 返回：0 成功；负数表示失败，取绝对值后为 MOTION_ERR_* 错误码
 *       （MOTION_ERR_PARAMETER / MOTION_ERR_SYNTAX / MOTION_ERR_VARIABLE /
 *        MOTION_ERR_RUNTIME）。
 * 优先级（由低到高）：OR < AND < 比较 < NOT < +- < * / % < 一元正负 < 原子。
 */
int motion_flow_eval(const char *expression,
                     const motion_variable_table_t *variables,
                     motion_value_t *result);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_FLOW_H */
