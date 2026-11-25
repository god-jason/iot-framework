#ifndef RS_FUNC_H
#define RS_FUNC_H

/*
 * roshell_func.h - roshell 内置函数 / 常量与扩展注册表内部声明
 *
 * 内置 math / 系统函数表项、内置常量表项（MATH 门控），
 * 扩展函数 / 常量注册表项与查表接口（EXPR 门控）。
 * 实现见 roshell_func.c，求值器在 roshell_eval.c 查表调用。
 * 仅供 roshell 各实现文件使用，平台 / 组件代码不应依赖。
 */

#include "roshell.h"
#include "roshell_eval.h"

/* ------------------------- 内置函数 / 常量表（MATH） ------------------------- */

#if RS_ENABLE_MATH

/*
 * 内置函数表项：numeric=true 为 math 函数（实参强制数值）；
 * 系统函数（tick/uptime/time/date/empty）自行处理实参类型。
 */
typedef struct rs_func {
    const char *name;
    uint8_t     argc;
    bool        numeric;
    void      (*fn)(const rs_value_t *args, rs_value_t *out);
} rs_func_t;

/* 内置常量表项（脚本中裸标识符引用，浮点值） */
typedef struct {
    const char *name;
    float       v;
} rs_const_t;

/* 按名查内置函数表（大小写敏感），未命中返回 NULL */
const rs_func_t *rs_func_find(const char *name, uint16_t len);

/* 按名查内置常量表（大小写敏感），未命中返回 NULL */
const rs_const_t *rs_const_find(const char *name, uint16_t len);

#endif /* RS_ENABLE_MATH */

/* -------------------- 扩展函数 / 常量注册表（EXPR） -------------------- */

#if RS_ENABLE_EXPR

typedef struct rs_ext_fn {
    char              name[RS_NAME_MAX];
    uint8_t           argc;
    rs_func_fn_t fn;
} rs_ext_fn_t;

typedef struct {
    char   name[RS_NAME_MAX];
    float  v;
} rs_ext_const_t;

/* 查扩展函数表（大小写敏感）；未命中返回 NULL */
const rs_ext_fn_t *rs_ext_func_find(const char *name, uint16_t len);

/* 查扩展常量表（大小写敏感）；未命中返回 NULL */
const rs_ext_const_t *rs_ext_const_find(const char *name, uint16_t len);

/* 裸标识符常量查找：内置常量（MATH）优先，其次扩展常量；命中写出浮点值 */
bool rs_find_const_num(const char *name, uint16_t len, float *v);

#endif /* RS_ENABLE_EXPR */

#endif /* RS_FUNC_H */
