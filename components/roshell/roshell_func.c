/*
 * roshell_func.c - roshell 内置函数 / 常量表与扩展注册表
 *
 * 内置 math 函数与系统函数表、内置常量表（RS_ENABLE_MATH 门控）；
 * 扩展函数 / 常量注册表与 rs_func_register / rs_const_register
 * 注册 API（RS_ENABLE_EXPR 门控）。求值器在 roshell_eval.c 查表调用。
 */

#include "roshell.h"
#include "roshell_func.h"
#include "roshell_lexer.h"
#include "ros.h"

#include <string.h>
#include <stdio.h>

#if RS_ENABLE_MATH
#include <math.h>
#include "hal_rtc.h"      /* time() / date() 时间源 */
#endif

/* ========================= 内置函数 / 常量表（MATH） ========================= */
/*
 * RS_ENABLE_MATH 门控。
 * 内置 math 函数表：名 + 形参个数 + numeric 标志 + 实现。
 * 实现假定 numeric 表项的 args 已由求值器（rs_func_apply）通过数值校验；
 * abs/min/max 在全部实参为整数时返回整数，其余一律返回浮点；
 * 域错误交给 libm（nan/inf 按现有浮点值语义传播）。
 * 系统函数（tick/uptime/time/date/empty，numeric=false）自行处理实参类型。
 * 求值器在 rs_eval.c 经 rs_func_find / rs_const_find 查本表。
 */
#if RS_ENABLE_MATH

/* 数值取值（整数提升为浮点） */
static float rs_m_fnum(const rs_value_t *v)
{
    return v->type == RS_VAL_FLOAT ? v->f : (float)v->i;
}

/* 浮点结果写回 */
static void rs_m_setf(rs_value_t *out, float v)
{
    out->type = RS_VAL_FLOAT;
    out->f = v;
}

static void rs_m_abs(const rs_value_t *a, rs_value_t *out)
{
    if (a[0].type == RS_VAL_INT) {
        int32_t v = a[0].i;
        out->type = RS_VAL_INT;
        out->i = (v < 0) ? (int32_t)(-(int64_t)v) : v;
    } else {
        rs_m_setf(out, fabsf(a[0].f));
    }
}

static void rs_m_floor(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, floorf(rs_m_fnum(&a[0])));
}

static void rs_m_ceil(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, ceilf(rs_m_fnum(&a[0])));
}

/* roundf：C99 舍入到最近整数，.5 远离零方向 */
static void rs_m_round(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, roundf(rs_m_fnum(&a[0])));
}

static void rs_m_sqrt(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, sqrtf(rs_m_fnum(&a[0])));
}

static void rs_m_sin(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, sinf(rs_m_fnum(&a[0])));
}

static void rs_m_cos(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, cosf(rs_m_fnum(&a[0])));
}

static void rs_m_tan(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, tanf(rs_m_fnum(&a[0])));
}

static void rs_m_asin(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, asinf(rs_m_fnum(&a[0])));
}

static void rs_m_acos(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, acosf(rs_m_fnum(&a[0])));
}

static void rs_m_atan(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, atanf(rs_m_fnum(&a[0])));
}

static void rs_m_exp(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, expf(rs_m_fnum(&a[0])));
}

static void rs_m_log(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, logf(rs_m_fnum(&a[0])));
}

static void rs_m_log10(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, log10f(rs_m_fnum(&a[0])));
}

static void rs_m_pow(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, powf(rs_m_fnum(&a[0]), rs_m_fnum(&a[1])));
}

static void rs_m_atan2(const rs_value_t *a, rs_value_t *out)
{
    rs_m_setf(out, atan2f(rs_m_fnum(&a[0]), rs_m_fnum(&a[1])));
}

static void rs_m_min(const rs_value_t *a, rs_value_t *out)
{
    if (a[0].type == RS_VAL_INT && a[1].type == RS_VAL_INT) {
        out->type = RS_VAL_INT;
        out->i = (a[0].i < a[1].i) ? a[0].i : a[1].i;
    } else {
        rs_m_setf(out, fminf(rs_m_fnum(&a[0]), rs_m_fnum(&a[1])));
    }
}

static void rs_m_max(const rs_value_t *a, rs_value_t *out)
{
    if (a[0].type == RS_VAL_INT && a[1].type == RS_VAL_INT) {
        out->type = RS_VAL_INT;
        out->i = (a[0].i > a[1].i) ? a[0].i : a[1].i;
    } else {
        rs_m_setf(out, fmaxf(rs_m_fnum(&a[0]), rs_m_fnum(&a[1])));
    }
}

/* ------------------------------ 系统函数 ------------------------------ */

/* tick()：内核 tick 毫秒计数（回绕后为负值，与 ros_tick_get 位宽一致） */
static void rs_f_tick(const rs_value_t *a, rs_value_t *out)
{
    (void)a;
    out->type = RS_VAL_INT;
    out->i = (int32_t)ros_tick_get();
}

/* uptime()：开机秒数 */
static void rs_f_uptime(const rs_value_t *a, rs_value_t *out)
{
    (void)a;
    out->type = RS_VAL_INT;
    out->i = (int32_t)(ros_tick_get() / 1000U);
}

/* time() 时间源：优先 RTC 时间戳；RTC 未设置 / 不可用（返回 0）时
 * 回退为开机秒数（PC 假时钟即走此路径，与 uptime() 同源） */
static int32_t rs_time_seconds(void)
{
    uint32_t ts = 0U;

    if (hal_rtc_get_timestamp(&ts) == HAL_OK && ts != 0U) {
        return (int32_t)ts;
    }
    return (int32_t)(ros_tick_get() / 1000U);
}

static void rs_f_time(const rs_value_t *a, rs_value_t *out)
{
    (void)a;
    out->type = RS_VAL_INT;
    out->i = rs_time_seconds();
}

/* 1970-01-01 起的天数 -> 年月日（Howard Hinnant civil_from_days 算法，
 * 公历 proleptic，不依赖 datetime 组件） */
static void rs_date_from_days(int64_t z, int *year, unsigned *month, unsigned *day)
{
    int64_t era;
    uint64_t doe, yoe, doy, mp;
    int64_t y;

    z += 719468;
    era = (z >= 0) ? (z / 146097) : ((z - 146096) / 146097);
    doe = (uint64_t)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *day = (unsigned)(doy - (153 * mp + 2) / 5 + 1);
    if (mp < 10U) {
        *month = (unsigned)mp + 3U;
    } else {
        *month = (unsigned)mp - 9U;
    }
    if (*month <= 2U) {
        y += 1;
    }
    *year = (int)y;
}

/* date()："YYYY-MM-DD"。RTC 有效（year >= 1970）直接格式化；否则由
 * time() 秒数换算。返回静态缓冲（求值期内只读、单行复用，12 字节） */
static char s_date_buf[12];

static void rs_f_date(const rs_value_t *a, rs_value_t *out)
{
    hal_rtc_time_t t;

    (void)a;
    if (hal_rtc_get_time(&t) == HAL_OK && t.year >= 1970U) {
        (void)snprintf(s_date_buf, sizeof(s_date_buf), "%04u-%02u-%02u",
                       (unsigned)t.year, (unsigned)t.month, (unsigned)t.day);
    } else {
        int y;
        unsigned m, d;

        rs_date_from_days((int64_t)rs_time_seconds() / 86400, &y, &m, &d);
        (void)snprintf(s_date_buf, sizeof(s_date_buf), "%04d-%02u-%02u",
                       y, m, d);
    }
    out->type = RS_VAL_STRING;
    out->s = s_date_buf;
    out->slen = (uint16_t)strlen(s_date_buf);
}

/* empty($x)：任意类型判空——字符串长度为 0 / 布尔为 false / 数值为 0。
 * 未定义变量在求值期即报 undefined variable，不会到达本函数 */
static void rs_f_empty(const rs_value_t *a, rs_value_t *out)
{
    out->type = RS_VAL_BOOL;
    switch (a[0].type) {
    case RS_VAL_STRING:
        out->b = (a[0].slen == 0U);
        break;
    case RS_VAL_BOOL:
        out->b = !a[0].b;
        break;
    case RS_VAL_INT:
        out->b = (a[0].i == 0);
        break;
    case RS_VAL_FLOAT:
        out->b = (a[0].f == 0.0f);
        break;
    default:
        out->b = true;
        break;
    }
}

static const rs_func_t rs_funcs[] = {
    { "abs",    1U, true,  rs_m_abs    },
    { "floor",  1U, true,  rs_m_floor  },
    { "ceil",   1U, true,  rs_m_ceil   },
    { "round",  1U, true,  rs_m_round  },
    { "sqrt",   1U, true,  rs_m_sqrt   },
    { "sin",    1U, true,  rs_m_sin    },
    { "cos",    1U, true,  rs_m_cos    },
    { "tan",    1U, true,  rs_m_tan    },
    { "asin",   1U, true,  rs_m_asin   },
    { "acos",   1U, true,  rs_m_acos   },
    { "atan",   1U, true,  rs_m_atan   },
    { "exp",    1U, true,  rs_m_exp    },
    { "log",    1U, true,  rs_m_log    },
    { "log10",  1U, true,  rs_m_log10  },
    { "pow",    2U, true,  rs_m_pow    },
    { "atan2",  2U, true,  rs_m_atan2  },
    { "min",    2U, true,  rs_m_min    },
    { "max",    2U, true,  rs_m_max    },
    { "tick",   0U, false, rs_f_tick   },
    { "uptime", 0U, false, rs_f_uptime },
    { "time",   0U, false, rs_f_time   },
    { "date",   0U, false, rs_f_date   },
    { "empty",  1U, false, rs_f_empty  }
};

#define RS_FUNC_COUNT (sizeof(rs_funcs) / sizeof(rs_funcs[0]))

const rs_func_t *rs_func_find(const char *name, uint16_t len)
{
    uint32_t i;

    for (i = 0U; i < RS_FUNC_COUNT; i++) {
        if (strlen(rs_funcs[i].name) == (size_t)len &&
            memcmp(rs_funcs[i].name, name, len) == 0) {
            return &rs_funcs[i];
        }
    }
    return NULL;
}

/* 内置常量表：脚本中裸标识符引用（大小写敏感），值为浮点 */
static const rs_const_t rs_consts[] = {
    { "PI", 3.14159265358979f },
    { "E",  2.718281828459045f }
};

#define RS_CONST_COUNT (sizeof(rs_consts) / sizeof(rs_consts[0]))

const rs_const_t *rs_const_find(const char *name, uint16_t len)
{
    uint32_t i;

    for (i = 0U; i < RS_CONST_COUNT; i++) {
        if (strlen(rs_consts[i].name) == (size_t)len &&
            memcmp(rs_consts[i].name, name, len) == 0) {
            return &rs_consts[i];
        }
    }
    return NULL;
}

#endif /* RS_ENABLE_MATH */

#if RS_ENABLE_EXPR
/* ========================= 扩展函数 / 常量注册表（EXPR） ========================= */
/*
 * 平台 / 组件通过 roshell.h 的 rs_func_register / rs_const_register
 * 注入全局函数与常量；表为进程级静态（不随实例、不支持反注册）。
 * 查找优先级：内置函数 -> 扩展函数 -> 脚本 function；
 * 常量：内置常量 -> 扩展常量。
 */

static vec_t s_ext_funcs;   /* rs_ext_fn_t：惰性初始化，容量上限 RS_EXT_FUNC_MAX */
static vec_t s_ext_consts;  /* rs_ext_const_t：容量上限 RS_EXT_CONST_MAX */

/* 扩展表惰性初始化：首次注册时建立；elem_size 非 0 表示已就绪 */
static bool rs_ext_ready(vec_t *v, uint32_t elem_size)
{
    if (v->elem_size != 0U) {
        return true;
    }
    return vec_init(v, elem_size, 0U) == 0;
}

/* 查扩展函数表（大小写敏感）；未命中返回 NULL */
const rs_ext_fn_t *rs_ext_func_find(const char *name, uint16_t len)
{
    uint32_t i;

    for (i = 0U; i < vec_size(&s_ext_funcs); i++) {
        const rs_ext_fn_t *x = (const rs_ext_fn_t *)vec_at(&s_ext_funcs, i);

        if (strlen(x->name) == (size_t)len && memcmp(x->name, name, len) == 0) {
            return x;
        }
    }
    return NULL;
}

/* 查扩展常量表（大小写敏感）；未命中返回 NULL */
const rs_ext_const_t *rs_ext_const_find(const char *name, uint16_t len)
{
    uint32_t i;

    for (i = 0U; i < vec_size(&s_ext_consts); i++) {
        const rs_ext_const_t *x =
            (const rs_ext_const_t *)vec_at(&s_ext_consts, i);

        if (strlen(x->name) == (size_t)len && memcmp(x->name, name, len) == 0) {
            return x;
        }
    }
    return NULL;
}

/* 裸标识符常量查找：内置常量（MATH）优先，其次扩展常量；命中写出浮点值 */
bool rs_find_const_num(const char *name, uint16_t len, float *v)
{
#if RS_ENABLE_MATH
    const rs_const_t *c = rs_const_find(name, len);

    if (c != NULL) {
        *v = c->v;
        return true;
    }
#endif
    {
        const rs_ext_const_t *x = rs_ext_const_find(name, len);

        if (x != NULL) {
            *v = (float)x->v;
            return true;
        }
    }
    return false;
}
#endif /* RS_ENABLE_EXPR */

/* ============================== 扩展注册 API ============================== */
/*
 * 向解释器注入全局函数 / 常量（规格：roshell.md 扩展注册）。
 * RS_ENABLE_EXPR=0 时表与求值路径均被裁剪，注册返回
 * RS_ERR_UNSUPPORTED（函数保留空实现，保证链接可用）。
 */

#if RS_ENABLE_EXPR

/* 扩展名合法性：标识符字符（字母 / 下划线开头），长度 < RS_NAME_MAX */
static bool rs_ext_name_ok(const char *name, uint16_t *len)
{
    uint16_t n = 0U;

    if (name == NULL || !rs_is_alpha(name[0])) {
        return false;
    }
    while (name[n] != '\0') {
        if (!rs_is_alpha(name[n]) && !rs_is_digit(name[n])) {
            return false;
        }
        n++;
    }
    if (n >= RS_NAME_MAX) {
        return false;
    }
    *len = n;
    return true;
}

/* 名称占用检查：内置函数 / 内置常量 / 扩展函数 / 扩展常量共用一个名字空间，
 * 任一命中即视为重名（注册返回 RS_ERR_PARAM） */
static bool rs_ext_taken(const char *name, uint16_t len)
{
#if RS_ENABLE_MATH
    if (rs_func_find(name, len) != NULL || rs_const_find(name, len) != NULL) {
        return true;
    }
#endif
    return rs_ext_func_find(name, len) != NULL ||
           rs_ext_const_find(name, len) != NULL;
}

int rs_func_register(const char *name, uint8_t argc, rs_func_fn_t fn)
{
    rs_ext_fn_t x;
    uint16_t len;

    if (fn == NULL || argc > RS_FUNC_ARGS_MAX || !rs_ext_name_ok(name, &len)) {
        return RS_ERR_PARAM;
    }
    if (rs_ext_taken(name, len)) {
        return RS_ERR_PARAM;
    }
    if (!rs_ext_ready(&s_ext_funcs, sizeof(rs_ext_fn_t))) {
        return RS_ERR_LIMIT;
    }
    if (vec_size(&s_ext_funcs) >= RS_EXT_FUNC_MAX) {
        return RS_ERR_LIMIT;
    }
    memset(&x, 0, sizeof(x));
    memcpy(x.name, name, (size_t)len + 1U);
    x.argc = argc;
    x.fn = fn;
    if (vec_push(&s_ext_funcs, &x) != 0) {
        return RS_ERR_LIMIT;
    }
    return RS_OK;
}

int rs_const_register(const char *name, double value)
{
    rs_ext_const_t x;
    uint16_t len;

    if (!rs_ext_name_ok(name, &len)) {
        return RS_ERR_PARAM;
    }
    if (rs_ext_taken(name, len)) {
        return RS_ERR_PARAM;
    }
    if (!rs_ext_ready(&s_ext_consts, sizeof(rs_ext_const_t))) {
        return RS_ERR_LIMIT;
    }
    if (vec_size(&s_ext_consts) >= RS_EXT_CONST_MAX) {
        return RS_ERR_LIMIT;
    }
    memset(&x, 0, sizeof(x));
    memcpy(x.name, name, (size_t)len + 1U);
    x.v = value;
    if (vec_push(&s_ext_consts, &x) != 0) {
        return RS_ERR_LIMIT;
    }
    return RS_OK;
}

#else /* !RS_ENABLE_EXPR */

int rs_func_register(const char *name, uint8_t argc, rs_func_fn_t fn)
{
    (void)name; (void)argc; (void)fn;
    return RS_ERR_UNSUPPORTED;
}

int rs_const_register(const char *name, double value)
{
    (void)name; (void)value;
    return RS_ERR_UNSUPPORTED;
}

#endif /* RS_ENABLE_EXPR */
