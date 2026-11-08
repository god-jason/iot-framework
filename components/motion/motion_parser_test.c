/* motion_parser.c 单元测试：指令、参数与值 token 解析。 */
#include "test.h"
#include "motion_parser.h"

static const motion_param_t *parser_find(const motion_instruction_t *ins,
                                         const char *name)
{
    uint8_t i;

    for (i = 0U; i < ins->param_count; i++) {
        if (strcmp(ins->params[i].name, name) == 0) {
            return &ins->params[i];
        }
    }
    return NULL;
}

void ut_motion_parser(void)
{
    motion_instruction_t ins;
    motion_value_t value;
    const motion_param_t *param;

    tf_begin("motion_parser/设备指令与内联参数");
    TF_CHECK_INT(motion_parser_parse("STEP 0 START, DIR 1, TIME 250ms", &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_INT(ins.command, MOTION_CMD_STEP);
    TF_CHECK_INT(ins.device_id, 0);
    TF_CHECK_STR(ins.action, "START");
    param = parser_find(&ins, "DIR");
    TF_CHECK(param != NULL);
    if (param != NULL) {
        TF_CHECK_INT(param->value.data.integer, 1);
    }
    param = parser_find(&ins, "TIME");
    TF_CHECK(param != NULL);
    if (param != NULL) {
        TF_CHECK_INT(param->value.data.integer, 250);
        TF_CHECK_INT(param->value.unit, MOTION_UNIT_MS);
    }
    tf_end();

    tf_begin("motion_parser/大小写与布尔字符串");
    TF_CHECK_INT(motion_parser_parse("gpio 2 set, value on", &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_INT(ins.command, MOTION_CMD_GPIO);
    TF_CHECK_INT(ins.device_id, 2);
    TF_CHECK_STR(ins.action, "SET");
    param = parser_find(&ins, "VALUE");
    TF_CHECK(param != NULL);
    if (param != NULL) {
        TF_CHECK_INT(param->value.type, MOTION_VALUE_BOOLEAN);
        TF_CHECK(param->value.data.boolean);
    }
    tf_end();

    tf_begin("motion_parser/值类型与单位换算");
    TF_CHECK_INT(motion_parser_parse_value("42", &value), 0);
    TF_CHECK_INT(value.type, MOTION_VALUE_INTEGER);
    TF_CHECK_INT(value.data.integer, 42);
    TF_CHECK_INT(motion_parser_parse_value("-7", &value), 0);
    TF_CHECK_INT(value.data.integer, -7);
    TF_CHECK_INT(motion_parser_parse_value("3.5", &value), 0);
    TF_CHECK_INT(value.type, MOTION_VALUE_FLOAT);
    TF_CHECK_NEAR(value.data.real, 3.5, 1e-9);
    TF_CHECK_INT(motion_parser_parse_value("1.5s", &value), 0);
    TF_CHECK_INT(value.data.integer, 1500);
    TF_CHECK_INT(value.unit, MOTION_UNIT_MS);
    TF_CHECK_INT(motion_parser_parse_value("2cm", &value), 0);
    TF_CHECK_INT(value.data.integer, 20);
    TF_CHECK_INT(value.unit, MOTION_UNIT_MM);
    TF_CHECK_INT(motion_parser_parse_value("5kHz", &value), 0);
    TF_CHECK_INT(value.data.integer, 5000);
    TF_CHECK_INT(value.unit, MOTION_UNIT_HZ);
    TF_CHECK_INT(motion_parser_parse_value("\"A;B//C\"", &value), 0);
    TF_CHECK_STR(value.data.string, "A;B//C");
    TF_CHECK_INT(motion_parser_parse_value("$VAR", &value), 0);
    TF_CHECK_INT(value.type, MOTION_VALUE_VARIABLE);
    TF_CHECK_STR(value.data.variable, "VAR");
    tf_end();

    tf_begin("motion_parser/表达式与非法输入");
    TF_CHECK_INT(motion_parser_parse("IF $A >= 10 AND $B", &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_INT(ins.command, MOTION_CMD_IF);
    TF_CHECK_STR(ins.expression, "$A >= 10 AND $B");
    TF_CHECK_INT(motion_parser_parse("LOOP 3", &ins), MOTION_PARSE_OK);
    TF_CHECK_INT(ins.command, MOTION_CMD_LOOP);
    TF_CHECK_STR(ins.expression, "3");
    TF_CHECK_INT(motion_parser_parse("WAIT ADC 0", &ins),
                 MOTION_PARSE_INVALID);
    TF_CHECK_INT(motion_parser_parse("START FOO", &ins),
                 MOTION_PARSE_INVALID);
    TF_CHECK_INT(motion_parser_parse("", &ins), MOTION_PARSE_EMPTY);
    TF_CHECK_STR(motion_parse_result_message(MOTION_PARSE_OK), "ok");
    TF_CHECK_STR(motion_parse_result_message(MOTION_PARSE_EMPTY),
                 "empty line");
    tf_end();
}
