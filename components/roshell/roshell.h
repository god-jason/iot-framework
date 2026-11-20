#ifndef ROSHELL_H
#define ROSHELL_H

/*
 * roshell.h - roshell 脚本语言解释器公共接口
 *
 * 形态：运行在 RTOS 之上的精简脚本解释器（语法见 roshell.md）。
 * 执行模型：直接解释源文本，不生成字节码；加载时仅做一次轻量扫描
 * （校验块配对、登记函数名与起始行），运行期按行号就地词法分析。
 *
 * 典型用法：
 *   roshell_t *rs = roshell_create();
 *   roshell_err_t err;
 *   roshell_exec(rs, "if $x == 1\nend\n", &err);
 *   roshell_destroy(rs);
 *
 * 变量作用域：每个执行单元（脚本 / 函数调用）私有；实例保留一个
 * 顶层执行单元，roshell_exec / roshell_exec_line / load 共用其变量表，
 * 便于交互式逐行执行与测试观察（roshell_get_* 读取顶层变量）。
 * run 派生任务拥有独立执行单元。
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "roshell_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 错误码 ============================== */

enum {
    RO_OK = 0,
    RO_ERR_PARAM         = -1,   /* 参数非法 */
    RO_ERR_PARSE         = -2,   /* 词法 / 语法错误（unknown statement 等） */
    RO_ERR_TYPE          = -3,   /* type mismatch */
    RO_ERR_UNDEF_VAR     = -4,   /* 变量未定义 */
    RO_ERR_UNDEF_FUNC    = -5,   /* undefined function */
    RO_ERR_DIV_ZERO      = -6,   /* 取余除零 */
    RO_ERR_RESOURCE      = -7,   /* 内存不足 / 任务创建失败 */
    RO_ERR_UNSUPPORTED   = -8,   /* 编译期裁剪掉的能力 */
    RO_ERR_NOT_FOUND     = -9,   /* 文件不可读 / 槽位无效 */
    RO_ERR_LIMIT         = -10,  /* 超出资源上限（行长 / 脚本长 / 列表项等） */
    RO_ERR_STACK_OVERFLOW = -11, /* call stack overflow */
    RO_ERR_ARG_COUNT     = -12,  /* arg count mismatch */
    RO_ERR_DUPLICATE     = -13,  /* duplicate function */
    RO_ERR_BLOCK         = -14,  /* unclosed block / unmatched end */
    RO_ERR_TIMEOUT       = -15   /* run 等待超时 */
};

/* run 槽位状态 */
enum {
    RO_RUN_FREE = 0,
    RO_RUN_RUNNING,
    RO_RUN_DONE,
    RO_RUN_ERROR
};

/* 错误详情：code 为上述错误码，line 为出错行号（1 起，0 表示未知） */
typedef struct {
    int      code;
    uint16_t line;
    char     msg[48];
} roshell_err_t;

/* ============================== 实例 ============================== */

typedef struct roshell roshell_t;

/* 创建实例（堆分配词法缓冲 / 变量表 / run 槽表）；失败返回 NULL */
roshell_t *roshell_create(void);

/* 销毁实例并释放全部堆内存；已结束的 run 任务 TCB 一并回收 */
void roshell_destroy(roshell_t *rs);

/* ============================== 执行 ============================== */

/*
 * 扫描并同步执行整段脚本（多行，块语句完整）。
 * 返回 RO_OK 或错误码；err 可为 NULL。
 */
int roshell_exec(roshell_t *rs, const char *script, roshell_err_t *err);

/*
 * 交互式执行单行（不留存全文）。块关键字报
 * "block statement requires script"。返回同上。
 */
int roshell_exec_line(roshell_t *rs, const char *line, roshell_err_t *err);

/*
 * 异步执行整段脚本：新建任务立即返回。
 * 返回槽位号（>=0）；负值为错误码。
 */
int roshell_run_async(roshell_t *rs, const char *script, roshell_err_t *err);

/* 等待槽位任务结束；返回 RO_OK / RO_ERR_TIMEOUT / 任务错误码 / RO_ERR_PARAM */
int roshell_run_wait(roshell_t *rs, int slot, uint32_t timeout_ms);

/* 查询槽位状态（RO_RUN_*）；槽位号非法返回 RO_ERR_PARAM */
int roshell_run_state(const roshell_t *rs, int slot);

#if ROSHELL_HAS_FS
/* 读取文件并同步执行（解析失败报告文件内行号）。返回同 roshell_exec */
int roshell_load(roshell_t *rs, const char *path, roshell_err_t *err);
#endif

/* ============================== 顶层变量读取 ============================== */
/* 供上位机 / 测试观察顶层执行单元变量；name 可带 $ 前缀。
 * 未定义返回 RO_ERR_UNDEF_VAR，类型不符返回 RO_ERR_TYPE。 */

int roshell_get_int(roshell_t *rs, const char *name, int32_t *out);
int roshell_get_float(roshell_t *rs, const char *name, float *out);
int roshell_get_bool(roshell_t *rs, const char *name, bool *out);
/* 拷贝字符串值到 buf（含结尾 0，截断安全） */
int roshell_get_str(roshell_t *rs, const char *name, char *buf, uint32_t cap);

#ifdef __cplusplus
}
#endif

#endif /* ROSHELL_H */
