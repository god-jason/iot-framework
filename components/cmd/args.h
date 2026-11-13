#ifndef ARGS_H
#define ARGS_H

/*
 * args.h - components/cmd 命令参数定义与解析（与命令表解耦）
 *
 * 用 args_def_t 描述参数，用 args_parse() 解析
 * "--name value"、"--name=value"、"-s value"、布尔开关及位置参数，
 * 再通过 args_get_*() 按定义下标取值。
 *
 * 参数模型：
 *   - positional=true 为位置参数（按声明顺序绑定，name 仅作显示标签）；
 *   - positional=false 为选项：--name / -x（short_name 非 0 时）；
 *   - ARGS_TYPE_BOOL 为开关，不带值（也接受 --flag=0/1/on/off）；
 *   - required=true 的参数必须出现（默认值不会满足 required）。
 *
 * 所有文本均为只读引用，不分配内存。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ARGS_MAX
#define ARGS_MAX            12U     /* 单条命令最多参数个数 */
#endif
#ifndef ARGS_NAME_MAX
#define ARGS_NAME_MAX       20U     /* 参数名最大长度（含结尾 0） */
#endif

/* 错误码 */
#define ARGS_OK                 0
#define ARGS_ERR_PARAM          (-1)    /* 参数非法（NULL / 下标越界） */
#define ARGS_ERR_UNKNOWN        (-2)    /* 未知选项 */
#define ARGS_ERR_MISSING_VALUE  (-3)    /* 选项缺少取值 */
#define ARGS_ERR_MISSING_ARG    (-4)    /* 缺少必填参数 */
#define ARGS_ERR_TYPE           (-5)    /* 取值类型错误 */
#define ARGS_ERR_EXTRA          (-6)    /* 多余的位置参数 */

/* 参数类型 */
typedef enum {
    ARGS_TYPE_STR = 0,      /* 字符串 */
    ARGS_TYPE_INT,          /* 有符号整数 */
    ARGS_TYPE_UINT,         /* 无符号整数 */
    ARGS_TYPE_BOOL          /* 布尔开关 */
} args_type_t;

/* 单个参数定义 */
typedef struct {
    const char *name;       /* 显示名（位置参数即标签；选项为长名，不含 --） */
    char        short_name; /* 短选项名（不含 -）；0 表示无 */
    uint8_t     type;       /* args_type_t */
    bool        positional; /* true 表示位置参数 */
    bool        required;   /* 是否必须出现 */
    const char *def;        /* 默认值（文本），可为 NULL */
    const char *desc;       /* 参数说明，可为 NULL */
} args_def_t;

/* 解析结果（由调用者分配，args_parse 填充） */
typedef struct {
    const args_def_t *args;             /* 参数定义表（由解析时绑定） */
    uint8_t     arg_count;              /* 参数个数 */
    const char *vals[ARGS_MAX];         /* 命中的原始值（BOOL 可为 NULL） */
    bool        present[ARGS_MAX];      /* 是否出现 */
    uint8_t     positionals;            /* 已绑定位置参数个数 */
    int         error;                  /* ARGS_OK 或 ARGS_ERR_* */
    char        err_arg[ARGS_NAME_MAX]; /* 出错参数名 */
} args_parsed_t;

/*
 * 解析参数。
 * args      : 参数定义表；
 * arg_count : 参数个数；
 * argv      : 参数数组，不含命令名本身；
 * argc      : 参数个数；
 * parsed    : 输出解析结果。
 * 返回      : ARGS_OK 或负错误码（同时写入 parsed->error）。
 */
int args_parse(const args_def_t *args, uint8_t arg_count,
               char *const *argv, int argc, args_parsed_t *parsed);

/* 按长名 / 短名查找参数下标，未找到返回 -1 */
int args_find(const args_def_t *args, uint8_t arg_count, const char *name);
int args_find_short(const args_def_t *args, uint8_t arg_count,
                    char short_name);

/*
 * 取值：未出现时回退到定义中的默认值。
 * index 为参数在定义表中的下标。
 * 返回 false 表示下标非法、无默认值或类型转换失败。
 */
bool args_get_str(const args_parsed_t *parsed, uint8_t index, const char **out);
bool args_get_int(const args_parsed_t *parsed, uint8_t index, int *out);
bool args_get_uint(const args_parsed_t *parsed, uint8_t index, uint32_t *out);
bool args_get_bool(const args_parsed_t *parsed, uint8_t index, bool *out);

/* 错误码转文本 */
const char *args_error_str(int err);

#ifdef __cplusplus
}
#endif

#endif /* ARGS_H */
