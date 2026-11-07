/* motion_variable.c 单元测试：变量表、引用解析与类型转换。 */
#include "test.h"
#include "motion_error.h"
#include "motion_variable.h"

void ut_motion_variable(void)
{
    motion_variable_table_t table;
    motion_value_t value;
    motion_value_t ref;
    motion_value_t out;
    uint16_t i;

    tf_begin("motion_variable/设置查询与清空");
    motion_variable_init(&table);
    TF_CHECK_INT(table.count, 0);
    memset(&value, 0, sizeof(value));
    value.type = MOTION_VALUE_INTEGER;
    value.data.integer = 42;
    TF_CHECK_INT(motion_variable_set(&table, "A", &value), 0);
    TF_CHECK_INT(table.count, 1);
    TF_CHECK(motion_variable_exists(&table, "A"));
    TF_CHECK(motion_variable_exists(&table, "$a"));
    TF_CHECK(motion_variable_get(&table, "A", &out));
    TF_CHECK_INT(out.data.integer, 42);
    TF_CHECK(!motion_variable_get(&table, "NOPE", &out));
    motion_variable_clear(&table);
    TF_CHECK_INT(table.count, 0);
    TF_CHECK(!motion_variable_exists(&table, "A"));
    tf_end();

    tf_begin("motion_variable/非法参数");
    TF_CHECK_INT(motion_variable_set(&table, "", &value),
                 MOTION_ERR_VARIABLE);
    TF_CHECK_INT(motion_variable_set(NULL, "A", &value),
                 MOTION_ERR_VARIABLE);
    TF_CHECK_INT(motion_variable_set(&table, "A", NULL),
                 MOTION_ERR_VARIABLE);
    tf_end();

    tf_begin("motion_variable/引用解析与类型转换");
    motion_variable_init(&table);
    memset(&value, 0, sizeof(value));
    value.type = MOTION_VALUE_STRING;
    strcpy(value.data.string, "12");
    TF_CHECK_INT(motion_variable_set(&table, "S", &value), 0);
    memset(&ref, 0, sizeof(ref));
    ref.type = MOTION_VALUE_VARIABLE;
    strcpy(ref.data.variable, "S");
    TF_CHECK_INT(motion_value_resolve(&ref, &table, &out), 0);
    TF_CHECK_INT(out.type, MOTION_VALUE_STRING);
    TF_CHECK_INT(motion_value_to_int(&out), 12);
    TF_CHECK_NEAR(motion_value_to_double(&out), 12.0, 1e-9);
    TF_CHECK(motion_value_to_bool(&out));
    strcpy(ref.data.variable, "NOPE");
    TF_CHECK_INT(motion_value_resolve(&ref, &table, &out),
                 MOTION_ERR_VARIABLE);
    TF_CHECK_INT(motion_value_resolve(NULL, &table, &out),
                 MOTION_ERR_PARAMETER);
    value.type = MOTION_VALUE_BOOLEAN;
    value.data.boolean = true;
    TF_CHECK_INT(motion_value_resolve(&value, &table, &out), 0);
    TF_CHECK(out.data.boolean);
    TF_CHECK_INT(motion_value_to_int(NULL), 0);
    TF_CHECK(!motion_value_to_bool(NULL));
    tf_end();

    tf_begin("motion_variable/容量上限");
    motion_variable_init(&table);
    memset(&value, 0, sizeof(value));
    value.type = MOTION_VALUE_INTEGER;
    for (i = 0U; i < MOTION_MAX_VARIABLES; i++) {
        char name[16];

        snprintf(name, sizeof(name), "V%u", (unsigned)i);
        TF_CHECK_INT(motion_variable_set(&table, name, &value), 0);
    }
    TF_CHECK_INT(table.count, MOTION_MAX_VARIABLES);
    TF_CHECK_INT(motion_variable_set(&table, "OVER", &value),
                 MOTION_ERR_VARIABLE);
    tf_end();
}
