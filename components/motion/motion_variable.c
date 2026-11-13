#include "motion_variable.h"
#include "motion_error.h"

/*
 * motion_variable.c —— 变量表与数值转换实现。
 *
 * 模块职责：
 *   - 以线性查找方式维护变量表（容量小，避免引入哈希表复杂度）；
 *   - 变量名归一化：去掉前导 '$' 并统一转大写，实现大小写不敏感；
 *   - 变量引用求值与各数值类型之间的安全转换。
 *
 * 数据流：motion_variable_set() 写入 -> 执行期 motion_value_resolve()
 *         或 motion_variable_get() 读取 -> to_int/to_double/to_bool 供运算。
 *
 * 边界约束：表容量固定为 MOTION_MAX_VARIABLES，满时返回错误而不扩容；
 *           所有名字写入都截断到 MOTION_MAX_VAR_NAME - 1 并补 '\0'。
 *           本文件不使用静态可变状态，可重入。
 */

#include "ros.h"
#include "common.h"

#include <stdlib.h>

/* 归一化变量名：去掉可选的前导 '$'，其余字符转大写后写入 out。
 * 参数：name 原始名字，可为 NULL；out 输出缓冲；size 缓冲容量。
 * 返回：无。size 为 0 时不做任何事；超出容量的字符被截断。 */
static void normalize_name(const char *name, char *out, size_t size)
{
    if (size == 0U) {
        return;
    }

    if (name != NULL && name[0] == '$') {
        name++;
    }

    str_copy(out, (uint32_t)size, name);
    (void)str_to_upper(out);
}

/* 初始化变量表：整体清零，count 置 0。 */
void motion_variable_init(motion_variable_table_t *table)
{
    if (table == NULL) {
        return;
    }
    memset(table, 0, sizeof(*table));
}

/* 清空变量表，复用初始化逻辑。 */
void motion_variable_clear(motion_variable_table_t *table)
{
    motion_variable_init(table);
}

/* 设置变量值：存在则覆盖，不存在则追加。
 * 参数：table 变量表；name 变量名；value 新值。
 * 返回：0 成功；MOTION_ERR_VARIABLE 参数非法、名字归一化后为空或表已满。 */
int motion_variable_set(motion_variable_table_t *table, const char *name,
                        const motion_value_t *value)
{
    char normalized[MOTION_MAX_VAR_NAME];
    uint16_t i;

    if (table == NULL || name == NULL || value == NULL) {
        return MOTION_ERR_VARIABLE;
    }

    normalize_name(name, normalized, sizeof(normalized));
    if (normalized[0] == '\0') {
        return MOTION_ERR_VARIABLE; /* 空变量名非法 */
    }

    /* 先查找同名变量，存在则就地覆盖 */
    for (i = 0U; i < table->count; i++) {
        if (strcmp(table->items[i].name, normalized) == 0) {
            table->items[i].value = *value;
            return 0;
        }
    }

    if (table->count >= MOTION_MAX_VARIABLES) {
        return MOTION_ERR_VARIABLE; /* 表满，无法新增 */
    }

    (void)str_copy(table->items[table->count].name,
                   sizeof(table->items[table->count].name), normalized);
    table->items[table->count].value = *value;
    table->count++;
    return 0;
}

/* 按名字查询变量。
 * 参数：table 变量表；name 变量名（可带 '$'）；value 输出值，可为 NULL。
 * 返回：true 找到；false 未找到或参数非法。 */
bool motion_variable_get(const motion_variable_table_t *table,
                         const char *name, motion_value_t *value)
{
    char normalized[MOTION_MAX_VAR_NAME];
    uint16_t i;

    if (table == NULL || name == NULL) {
        return false;
    }

    normalize_name(name, normalized, sizeof(normalized));

    for (i = 0U; i < table->count; i++) {
        if (strcmp(table->items[i].name, normalized) == 0) {
            if (value != NULL) {
                *value = table->items[i].value;
            }
            return true;
        }
    }

    return false;
}

/* 判断变量是否存在（内部复用 get，不取值）。 */
bool motion_variable_exists(const motion_variable_table_t *table,
                            const char *name)
{
    return motion_variable_get(table, name, NULL);
}

/* 解析可能的变量引用：VARIABLE 类型查表取值，其他类型直接复制。
 * 参数：value 输入值；table 变量表；out 输出值。
 * 返回：0 成功；MOTION_ERR_PARAMETER 参数为空；
 *       MOTION_ERR_VARIABLE 引用的变量不存在。
 * 说明：只解析一层，变量里再存变量引用不会递归求值。 */
int motion_value_resolve(const motion_value_t *value,
                         const motion_variable_table_t *table,
                         motion_value_t *out)
{
    if (value == NULL || out == NULL) {
        return MOTION_ERR_PARAMETER;
    }

    if (value->type == MOTION_VALUE_VARIABLE) {
        if (!motion_variable_get(table, value->data.variable, out)) {
            return MOTION_ERR_VARIABLE;
        }
        return 0;
    }

    *out = *value;
    return 0;
}

/* 转为 int64：整数直取，浮点截断，布尔取 0/1，字符串按十进制解析。 */
int64_t motion_value_to_int(const motion_value_t *value)
{
    if (value == NULL) {
        return 0;
    }

    switch (value->type) {
    case MOTION_VALUE_INTEGER:
        return value->data.integer;
    case MOTION_VALUE_FLOAT:
        return (int64_t)value->data.real;
    case MOTION_VALUE_BOOLEAN:
        return value->data.boolean ? 1 : 0;
    case MOTION_VALUE_STRING:
        return (int64_t)strtoll(value->data.string, NULL, 10);
    default:
        return 0; /* NONE / VARIABLE 未求值时按 0 处理 */
    }
}

/* 转为 double：整数提升，布尔取 0.0/1.0，字符串按浮点解析。 */
double motion_value_to_double(const motion_value_t *value)
{
    if (value == NULL) {
        return 0.0;
    }

    switch (value->type) {
    case MOTION_VALUE_INTEGER:
        return (double)value->data.integer;
    case MOTION_VALUE_FLOAT:
        return value->data.real;
    case MOTION_VALUE_BOOLEAN:
        return value->data.boolean ? 1.0 : 0.0;
    case MOTION_VALUE_STRING:
        return strtod(value->data.string, NULL);
    default:
        return 0.0;
    }
}

/* 转为 bool：非零数值、非空字符串为 true。 */
bool motion_value_to_bool(const motion_value_t *value)
{
    if (value == NULL) {
        return false;
    }

    switch (value->type) {
    case MOTION_VALUE_INTEGER:
        return value->data.integer != 0;
    case MOTION_VALUE_FLOAT:
        return value->data.real != 0.0;
    case MOTION_VALUE_BOOLEAN:
        return value->data.boolean;
    case MOTION_VALUE_STRING:
        return value->data.string[0] != '\0';
    default:
        return false;
    }
}
