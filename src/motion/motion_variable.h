#ifndef MOTION_VARIABLE_H
#define MOTION_VARIABLE_H

/*
 * motion_variable.h —— MotionScript 变量表与数值转换接口。
 *
 * 职责：以定长数组维护"变量名 -> motion_value_t"的映射，并提供
 *       变量引用求值以及 int / double / bool 之间的转换。
 *
 * 数据流：解析阶段把 $变量 原样保存在 motion_value_t 中；
 *         执行阶段调用 motion_value_resolve() 查出真实值后再运算。
 *
 * 边界约束：变量总数上限 MOTION_MAX_VARIABLES，变量名上限
 *           MOTION_MAX_VAR_NAME（含 '\0'），全部为静态存储，无动态分配。
 *           变量名比较大小写不敏感，写入前统一转大写。
 */

#include "motion_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 变量表中的一个条目 */
typedef struct {
    char name[MOTION_MAX_VAR_NAME]; /* 已归一化（去 '$'、全大写）的变量名 */
    motion_value_t value;           /* 变量当前值；若为 VARIABLE 类型则指向另一个变量 */
} motion_variable_t;

/* 变量表：定长数组 + 已用条数 */
typedef struct {
    motion_variable_t items[MOTION_MAX_VARIABLES]; /* 变量存储区 */
    uint16_t count;                                /* 已使用条目数，0 ~ MOTION_MAX_VARIABLES */
} motion_variable_table_t;

/* 初始化（等价于清空）变量表。table 为 NULL 时静默返回。 */
void motion_variable_init(motion_variable_table_t *table);
/* 清空变量表，语义同 motion_variable_init。 */
void motion_variable_clear(motion_variable_table_t *table);

/* 设置变量值：已存在则覆盖，不存在则新建。
 * 参数：table 变量表；name 变量名（可带前导 '$'，大小写不敏感）；
 *       value 待写入的值（按值复制）。
 * 返回：0 成功；MOTION_ERR_VARIABLE 参数非法、名字为空或表已满。 */
int motion_variable_set(motion_variable_table_t *table, const char *name,
                        const motion_value_t *value);
/* 查询变量值。
 * 参数：table 变量表；name 变量名；value 输出值，可为 NULL（只判断存在性）。
 * 返回：true 找到；false 未找到或参数非法。 */
bool motion_variable_get(const motion_variable_table_t *table,
                         const char *name, motion_value_t *value);
/* 判断变量是否存在；返回 true 存在。 */
bool motion_variable_exists(const motion_variable_table_t *table,
                            const char *name);

/* 若 value 是变量引用则查表，否则直接复制。
 * 参数：value 输入值；table 变量表；out 输出结果。
 * 返回：0 成功；MOTION_ERR_PARAMETER 参数为空；
 *       MOTION_ERR_VARIABLE 引用的变量不存在。 */
int motion_value_resolve(const motion_value_t *value,
                         const motion_variable_table_t *table,
                         motion_value_t *out);

/* 将任意类型值转为 int64：浮点截断，布尔取 0/1，字符串按十进制解析。 */
int64_t motion_value_to_int(const motion_value_t *value);
/* 将任意类型值转为 double：字符串按浮点解析。 */
double motion_value_to_double(const motion_value_t *value);
/* 将任意类型值转为 bool：非零数值 / 非空字符串为 true。 */
bool motion_value_to_bool(const motion_value_t *value);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_VARIABLE_H */
