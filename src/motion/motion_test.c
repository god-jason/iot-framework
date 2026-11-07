/*
 * MotionScript 解释器单元测试。
 * 覆盖解析、表达式、流程、设备、等待、错误、JOB 与边界条件。
 */
#include "test.h"

#include "motion.h"
#include "motion_device.h"
#include "motion_error.h"
#include "motion_flow.h"
#include "motion_parser.h"
#include "motion_task.h"
#include "motion_variable.h"
#include "motion_source.h"

#include "pc.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Mock 设备                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    int step_start_calls;
    int step_stop_calls;
    int step_speed_calls;
    int step_dir_calls;
    int step_enable_calls;
    int step_home_calls;
    int step_busy;
    int step_position;
    int last_step_dir;
    double last_step_speed;
    motion_unit_t last_step_unit;
    int32_t last_pulse;
    uint32_t last_time_ms;
    uint32_t last_acc_ms;
    uint32_t last_dec_ms;

    int pwm_start_calls;
    int pwm_duty_calls;
    int pwm_freq_calls;
    int pwm_stop_calls;
    int pwm_busy;
    uint32_t last_pwm_freq;
    uint32_t last_pwm_duty;
    uint32_t last_pwm_accel;

    int gpio_writes;
    int gpio_write_count[8];
    int gpio_values[8];
    int last_gpio_id;
    int last_gpio_value;

    int relay_write_count[8];
    int relay_values[8];
    int last_relay_id;
    int last_relay_value;

    int adc_read_calls;
    int adc_value;

    int sensor_value;

    /* 失败注入：置 1 时对应回调返回 -1 */
    int fail_step_start;
    int fail_step_stop;
    int fail_step_speed;
    int fail_step_dir;
    int fail_step_enable;
    int fail_step_home;
    int fail_step_position;
    int fail_step_busy;

    int fail_pwm_start;
    int fail_pwm_duty;
    int fail_pwm_freq;
    int fail_pwm_stop;
    int fail_pwm_busy;

    int fail_gpio_write;
    int fail_gpio_read;
    int fail_relay_write;
    int fail_relay_read;
    int fail_adc_read;
    int fail_sensor_read;
} mock_t;

static mock_t g_mock;

static int mock_step_start(uint16_t id, uint8_t dir, double speed,
                           motion_unit_t unit, int32_t pulse,
                           uint32_t time_ms, uint32_t acc_ms,
                           uint32_t dec_ms)
{
    (void)id;
    if (g_mock.fail_step_start) { return -1; }
    g_mock.step_start_calls++;
    g_mock.last_step_dir = dir;
    g_mock.last_step_speed = speed;
    g_mock.last_step_unit = unit;
    g_mock.last_pulse = pulse;
    g_mock.last_time_ms = time_ms;
    g_mock.last_acc_ms = acc_ms;
    g_mock.last_dec_ms = dec_ms;
    g_mock.step_busy = 1;
    return 0;
}

static int mock_step_stop(uint16_t id, uint32_t dec_ms)
{
    (void)id;
    if (g_mock.fail_step_stop) { return -1; }
    g_mock.step_stop_calls++;
    g_mock.last_dec_ms = dec_ms;
    g_mock.step_busy = 0;
    return 0;
}

static int mock_step_set_speed(uint16_t id, double speed, motion_unit_t unit)
{
    (void)id;
    if (g_mock.fail_step_speed) { return -1; }
    g_mock.step_speed_calls++;
    g_mock.last_step_speed = speed;
    g_mock.last_step_unit = unit;
    return 0;
}

static int mock_step_set_dir(uint16_t id, uint8_t dir)
{
    (void)id;
    if (g_mock.fail_step_dir) { return -1; }
    g_mock.step_dir_calls++;
    g_mock.last_step_dir = dir;
    return 0;
}

static int mock_step_enable(uint16_t id, uint8_t enable)
{
    (void)id;
    (void)enable;
    if (g_mock.fail_step_enable) { return -1; }
    g_mock.step_enable_calls++;
    return 0;
}

static int mock_step_home(uint16_t id, double speed, motion_unit_t unit,
                          uint8_t dir, uint32_t timeout_ms)
{
    (void)id;
    (void)timeout_ms;
    if (g_mock.fail_step_home) { return -1; }
    g_mock.step_home_calls++;
    g_mock.last_step_speed = speed;
    g_mock.last_step_unit = unit;
    g_mock.last_step_dir = dir;
    g_mock.step_busy = 1;
    return 0;
}

static int mock_step_is_busy(uint16_t id)
{
    (void)id;
    return g_mock.fail_step_busy ? -1 : g_mock.step_busy;
}

static int mock_step_position_read(uint16_t id, int32_t *position)
{
    (void)id;
    if (g_mock.fail_step_position) { return -1; }
    if (position != NULL) {
        *position = g_mock.step_position;
    }
    return 0;
}

static int mock_step_position_set(uint16_t id, int32_t position)
{
    (void)id;
    if (g_mock.fail_step_position) { return -1; }
    g_mock.step_position = position;
    return 0;
}

static int mock_pwm_start(uint16_t id, uint32_t freq, uint16_t duty,
                          uint32_t accel)
{
    (void)id;
    if (g_mock.fail_pwm_start) { return -1; }
    g_mock.pwm_start_calls++;
    g_mock.last_pwm_freq = freq;
    g_mock.last_pwm_duty = duty;
    g_mock.last_pwm_accel = accel;
    g_mock.pwm_busy = 1;
    return 0;
}

static int mock_pwm_set_duty(uint16_t id, uint16_t duty)
{
    (void)id;
    if (g_mock.fail_pwm_duty) { return -1; }
    g_mock.pwm_duty_calls++;
    g_mock.last_pwm_duty = duty;
    return 0;
}

static int mock_pwm_set_freq(uint16_t id, uint32_t freq)
{
    (void)id;
    if (g_mock.fail_pwm_freq) { return -1; }
    g_mock.pwm_freq_calls++;
    g_mock.last_pwm_freq = freq;
    return 0;
}

static int mock_pwm_stop(uint16_t id, uint32_t decel)
{
    (void)id;
    (void)decel;
    if (g_mock.fail_pwm_stop) { return -1; }
    g_mock.pwm_stop_calls++;
    g_mock.pwm_busy = 0;
    return 0;
}

static int mock_pwm_is_busy(uint16_t id)
{
    (void)id;
    return g_mock.fail_pwm_busy ? -1 : g_mock.pwm_busy;
}

static int mock_gpio_write(uint16_t id, uint8_t value)
{
    if (g_mock.fail_gpio_write) { return -1; }
    g_mock.gpio_writes++;
    g_mock.last_gpio_id = (int)id;
    g_mock.last_gpio_value = (int)value;
    if (id < 8U) {
        g_mock.gpio_values[id] = (int)value;
        g_mock.gpio_write_count[id]++;
    }
    return 0;
}

static int mock_gpio_read(uint16_t id, uint8_t *value)
{
    if (g_mock.fail_gpio_read) { return -1; }
    if (value == NULL) {
        return -1;
    }
    *value = (uint8_t)((id < 8U) ? g_mock.gpio_values[id] : 0);
    return 0;
}

static int mock_relay_write(uint16_t id, uint8_t value)
{
    if (g_mock.fail_relay_write) { return -1; }
    g_mock.last_relay_id = (int)id;
    g_mock.last_relay_value = (int)value;
    if (id < 8U) {
        g_mock.relay_values[id] = (int)value;
        g_mock.relay_write_count[id]++;
    }
    return 0;
}

static int mock_relay_read(uint16_t id, uint8_t *value)
{
    if (g_mock.fail_relay_read) { return -1; }
    if (value == NULL) {
        return -1;
    }
    *value = (uint8_t)((id < 8U) ? g_mock.relay_values[id] : 0);
    return 0;
}

static int mock_adc_read(uint16_t id, int32_t *value)
{
    (void)id;
    if (g_mock.fail_adc_read) { return -1; }
    g_mock.adc_read_calls++;
    if (value != NULL) {
        *value = g_mock.adc_value;
    }
    return 0;
}

static int mock_sensor_read(uint16_t id, int32_t *value)
{
    (void)id;
    if (g_mock.fail_sensor_read) { return -1; }
    if (value != NULL) {
        *value = g_mock.sensor_value;
    }
    return 0;
}

static const motion_device_ops_t g_mock_ops = {
    .step_start = mock_step_start,
    .step_stop = mock_step_stop,
    .step_set_speed = mock_step_set_speed,
    .step_set_dir = mock_step_set_dir,
    .step_enable = mock_step_enable,
    .step_home = mock_step_home,
    .step_is_busy = mock_step_is_busy,
    .step_position_read = mock_step_position_read,
    .step_position_set = mock_step_position_set,
    .pwm_start = mock_pwm_start,
    .pwm_set_duty = mock_pwm_set_duty,
    .pwm_set_freq = mock_pwm_set_freq,
    .pwm_stop = mock_pwm_stop,
    .pwm_is_busy = mock_pwm_is_busy,
    .gpio_write = mock_gpio_write,
    .gpio_read = mock_gpio_read,
    .relay_write = mock_relay_write,
    .relay_read = mock_relay_read,
    .adc_read = mock_adc_read,
    .sensor_read = mock_sensor_read
};

static void reset_all(void)
{
    iot_test_reset_state();
    (void)motion_init();
    memset(&g_mock, 0, sizeof(g_mock));
    (void)motion_device_register(&g_mock_ops);
}

static bool is_terminal(motion_state_t state)
{
    return state == MOTION_STATE_COMPLETED ||
           state == MOTION_STATE_ERROR ||
           state == MOTION_STATE_ABORTED ||
           state == MOTION_STATE_IDLE;
}

static motion_state_t run_to_terminal(int max_polls)
{
    int i;

    for (i = 0; i < max_polls; i++) {
        if (is_terminal(motion_state()) && i > 0) {
            break;
        }
        motion_poll();
    }
    return motion_state();
}

static const motion_param_t *find_param(const motion_instruction_t *ins,
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

/* ------------------------------------------------------------------ */
/* 1. 解析器                                                           */
/* ------------------------------------------------------------------ */

static void test_parser(void)
{
    motion_instruction_t ins;
    const motion_param_t *p;

    tf_begin("motion/解析 基础指令与大小写");
    TF_CHECK_INT(motion_parser_parse(
                     "step 0 start, dir 1, speed 1000rpm, time 1000ms",
                     &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_INT(ins.command, MOTION_CMD_STEP);
    TF_CHECK_INT(ins.device_id, 0);
    TF_CHECK_STR(ins.action, "START");
    TF_CHECK_INT(ins.param_count, 3);
    p = find_param(&ins, "DIR");
    TF_CHECK(p != NULL);
    TF_CHECK_INT(p->value.data.integer, 1);
    p = find_param(&ins, "SPEED");
    TF_CHECK(p != NULL);
    TF_CHECK_INT(p->value.data.integer, 1000);
    TF_CHECK_INT(p->value.unit, MOTION_UNIT_RPM);
    p = find_param(&ins, "TIME");
    TF_CHECK(p != NULL);
    TF_CHECK_INT(p->value.data.integer, 1000);
    TF_CHECK_INT(p->value.unit, MOTION_UNIT_MS);
    tf_end();

    tf_begin("motion/解析 注释与空行");
    TF_CHECK_INT(motion_parser_parse("; comment", &ins),
                 MOTION_PARSE_EMPTY);
    TF_CHECK_INT(motion_parser_parse("   ", &ins), MOTION_PARSE_EMPTY);
    TF_CHECK_INT(motion_parser_parse("// comment", &ins),
                 MOTION_PARSE_EMPTY);
    TF_CHECK_INT(motion_parser_parse(
                     "STEP 0 START ; start motor", &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_INT(ins.param_count, 0);
    TF_CHECK_INT(motion_parser_parse("STEP 0 START // x", &ins),
                 MOTION_PARSE_OK);
    tf_end();

    tf_begin("motion/解析 单位换算与默认单位");
    TF_CHECK_INT(motion_parser_parse("WAIT TIME 2min", &ins),
                 MOTION_PARSE_OK);
    p = find_param(&ins, "VALUE");
    TF_CHECK(p != NULL);
    TF_CHECK_INT(p->value.data.integer, 120000);
    TF_CHECK_INT(p->value.unit, MOTION_UNIT_MS);

    TF_CHECK_INT(motion_parser_parse("WAIT TIME 1h", &ins),
                 MOTION_PARSE_OK);
    p = find_param(&ins, "VALUE");
    TF_CHECK_INT(p->value.data.integer, 3600000);

    TF_CHECK_INT(motion_parser_parse(
                     "PWM 0 START, FREQ 10kHz, DUTY 50%", &ins),
                 MOTION_PARSE_OK);
    p = find_param(&ins, "FREQ");
    TF_CHECK_INT(p->value.data.integer, 10000);
    TF_CHECK_INT(p->value.unit, MOTION_UNIT_HZ);
    p = find_param(&ins, "DUTY");
    TF_CHECK_INT(p->value.data.integer, 50);
    TF_CHECK_INT(p->value.unit, MOTION_UNIT_PERCENT);

    TF_CHECK_INT(motion_parser_parse(
                     "STEP 0 START, SPEED 1000, ACC 1s, DEC 1s", &ins),
                 MOTION_PARSE_OK);
    p = find_param(&ins, "SPEED");
    TF_CHECK_INT(p->value.unit, MOTION_UNIT_STEP_S);
    p = find_param(&ins, "ACC");
    TF_CHECK_INT(p->value.data.integer, 1000);
    TF_CHECK_INT(p->value.unit, MOTION_UNIT_MS);

    TF_CHECK_INT(motion_parser_parse("WAIT TIME 1000", &ins),
                 MOTION_PARSE_OK);
    p = find_param(&ins, "VALUE");
    TF_CHECK_INT(p->value.unit, MOTION_UNIT_MS);
    tf_end();

    tf_begin("motion/解析 变量、字符串与设备读参数");
    TF_CHECK_INT(motion_parser_parse(
                     "SET $SPEED = $SPEED + 100", &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_INT(ins.command, MOTION_CMD_SET);
    TF_CHECK_STR(ins.name, "SPEED");
    TF_CHECK_STR(ins.expression, "$SPEED + 100");

    TF_CHECK_INT(motion_parser_parse("GPIO 2 READ, TO $STATE", &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_INT(ins.command, MOTION_CMD_GPIO);
    TF_CHECK_INT(ins.device_id, 2);
    TF_CHECK_STR(ins.action, "READ");
    p = find_param(&ins, "TO");
    TF_CHECK(p != NULL);
    TF_CHECK_INT(p->value.type, MOTION_VALUE_VARIABLE);
    TF_CHECK_STR(p->value.data.variable, "STATE");

    TF_CHECK_INT(motion_parser_parse("ERROR \"WEIGHT ERROR\"", &ins),
                 MOTION_PARSE_OK);
    p = find_param(&ins, "MSG");
    TF_CHECK(p != NULL);
    TF_CHECK_STR(p->value.data.string, "WEIGHT ERROR");

    TF_CHECK_INT(motion_parser_parse("ERROR 1008", &ins),
                 MOTION_PARSE_OK);
    p = find_param(&ins, "CODE");
    TF_CHECK(p != NULL);
    TF_CHECK_INT(p->value.data.integer, 1008);
    tf_end();

    tf_begin("motion/解析 位置、任务与等待指令");
    TF_CHECK_INT(motion_parser_parse("STEP 0 POSITION SET 0", &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_STR(ins.action, "POSITION");
    TF_CHECK_STR(ins.subaction, "SET");
    p = find_param(&ins, "VALUE");
    TF_CHECK_INT(p->value.data.integer, 0);

    TF_CHECK_INT(motion_parser_parse(
                     "STEP 0 POSITION READ, TO $POS", &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_STR(ins.subaction, "READ");
    p = find_param(&ins, "TO");
    TF_CHECK(p != NULL);
    TF_CHECK_STR(p->value.data.variable, "POS");

    TF_CHECK_INT(motion_parser_parse("JOB FEED", &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_INT(ins.command, MOTION_CMD_JOB);
    TF_CHECK_STR(ins.name, "FEED");

    TF_CHECK_INT(motion_parser_parse("START JOB FEED", &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_INT(ins.command, MOTION_CMD_START_JOB);
    TF_CHECK_STR(ins.name, "FEED");

    TF_CHECK_INT(motion_parser_parse("JOB FEED STATE, TO $S", &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_INT(ins.command, MOTION_CMD_JOB_STATE);
    p = find_param(&ins, "TO");
    TF_CHECK(p != NULL);

    TF_CHECK_INT(motion_parser_parse("WAIT GPIO 0, VALUE 1, TIMEOUT 10s",
                                     &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_INT(ins.command, MOTION_CMD_WAIT);
    TF_CHECK_STR(ins.action, "GPIO");
    TF_CHECK_INT(ins.device_id, 0);
    p = find_param(&ins, "TIMEOUT");
    TF_CHECK_INT(p->value.data.integer, 10000);
    tf_end();

    tf_begin("motion/解析 非法与超长输入");
    TF_CHECK(motion_parser_parse("NOT_A_COMMAND", &ins) !=
             MOTION_PARSE_OK);
    TF_CHECK(motion_parser_parse("STEP", &ins) != MOTION_PARSE_OK);
    TF_CHECK(motion_parser_parse("SET $A 1", &ins) != MOTION_PARSE_OK);
    {
        char long_line[400];

        memset(long_line, 'A', sizeof(long_line) - 1U);
        long_line[sizeof(long_line) - 1U] = '\0';
        TF_CHECK_INT(motion_parser_parse(long_line, &ins),
                     MOTION_PARSE_OVERFLOW);
    }
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 2. 变量与表达式                                                     */
/* ------------------------------------------------------------------ */

static void test_variables(void)
{
    motion_variable_table_t table;
    motion_value_t value;
    motion_value_t result;

    tf_begin("motion/变量 设置读取与覆盖");
    motion_variable_init(&table);
    memset(&value, 0, sizeof(value));
    value.type = MOTION_VALUE_INTEGER;
    value.data.integer = 42;
    TF_CHECK_INT(motion_variable_set(&table, "$A", &value), 0);
    TF_CHECK(motion_variable_exists(&table, "a"));
    TF_CHECK(motion_variable_get(&table, "A", &result));
    TF_CHECK_INT(result.data.integer, 42);

    value.data.integer = 7;
    TF_CHECK_INT(motion_variable_set(&table, "A", &value), 0);
    TF_CHECK(motion_variable_get(&table, "A", &result));
    TF_CHECK_INT(result.data.integer, 7);
    TF_CHECK_INT(table.count, 1);
    tf_end();

    tf_begin("motion/表达式 四则与优先级");
    motion_variable_init(&table);
    memset(&value, 0, sizeof(value));
    value.type = MOTION_VALUE_INTEGER;
    value.data.integer = 100;
    motion_variable_set(&table, "A", &value);
    value.type = MOTION_VALUE_FLOAT;
    value.data.real = 12.5;
    motion_variable_set(&table, "B", &value);
    value.type = MOTION_VALUE_STRING;
    strcpy(value.data.string, "FEED");
    motion_variable_set(&table, "S", &value);
    value.type = MOTION_VALUE_BOOLEAN;
    value.data.boolean = true;
    motion_variable_set(&table, "E", &value);

    TF_CHECK_INT(motion_flow_eval("$A + 100", &table, &result), 0);
    TF_CHECK_INT(motion_value_to_int(&result), 200);
    TF_CHECK_INT(motion_flow_eval("$A - 40", &table, &result), 0);
    TF_CHECK_INT(motion_value_to_int(&result), 60);
    TF_CHECK_INT(motion_flow_eval("$A * 2", &table, &result), 0);
    TF_CHECK_INT(motion_value_to_int(&result), 200);
    TF_CHECK_INT(motion_flow_eval("$A / 4", &table, &result), 0);
    TF_CHECK_INT(motion_value_to_int(&result), 25);
    TF_CHECK_INT(motion_flow_eval("$A % 7", &table, &result), 0);
    TF_CHECK_INT(motion_value_to_int(&result), 2);
    TF_CHECK_INT(motion_flow_eval("$B * 2", &table, &result), 0);
    TF_CHECK_INT(result.type, MOTION_VALUE_FLOAT);
    TF_CHECK_NEAR(result.data.real, 25.0, 1e-9);
    TF_CHECK_INT(motion_flow_eval("($A + 2) * 3", &table, &result), 0);
    TF_CHECK_INT(motion_value_to_int(&result), 306);
    tf_end();

    tf_begin("motion/表达式 比较与逻辑");
    TF_CHECK_INT(motion_flow_eval("$A > 50 AND $A < 200", &table,
                                  &result), 0);
    TF_CHECK(motion_value_to_bool(&result));
    TF_CHECK_INT(motion_flow_eval("$A == 1 OR $E", &table, &result), 0);
    TF_CHECK(motion_value_to_bool(&result));
    TF_CHECK_INT(motion_flow_eval("NOT $E", &table, &result), 0);
    TF_CHECK(!motion_value_to_bool(&result));
    TF_CHECK_INT(motion_flow_eval("$S == \"FEED\"", &table, &result), 0);
    TF_CHECK(motion_value_to_bool(&result));
    TF_CHECK_INT(motion_flow_eval("$A != 100", &table, &result), 0);
    TF_CHECK(!motion_value_to_bool(&result));
    TF_CHECK(motion_flow_eval("$MISSING + 1", &table, &result) != 0);
    TF_CHECK(motion_flow_eval("$A / 0", &table, &result) != 0);
    TF_CHECK(motion_flow_eval("$A +", &table, &result) != 0);
    tf_end();

    tf_begin("motion/SET 与 IF ELSE");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "SET $A = 10\n"
                     "SET $A = $A + 5\n"
                     "IF $A == 15\n"
                     "GPIO 0 ON\n"
                     "ELSE\n"
                     "GPIO 0 OFF\n"
                     "END\n"),
                 0);
    TF_CHECK_INT(motion_start(), 0);
    TF_CHECK_INT(run_to_terminal(10), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_write_count[0], 1);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 3. 流程控制                                                         */
/* ------------------------------------------------------------------ */

static void test_flow(void)
{
    tf_begin("motion/流程 IF 假分支与 ELSE");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "SET $A = 0\n"
                     "IF $A\n"
                     "GPIO 0 ON\n"
                     "ELSE\n"
                     "GPIO 1 ON\n"
                     "END\n"),
                 0);
    motion_start();
    TF_CHECK_INT(run_to_terminal(10), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_write_count[0], 0);
    TF_CHECK_INT(g_mock.gpio_write_count[1], 1);
    tf_end();

    tf_begin("motion/流程 LOOP 固定次数");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "SET $I = 0\n"
                     "LOOP 3\n"
                     "SET $I = $I + 1\n"
                     "ENDLOOP\n"
                     "IF $I == 3\n"
                     "GPIO 2 ON\n"
                     "END\n"),
                 0);
    motion_start();
    TF_CHECK_INT(run_to_terminal(20), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_write_count[2], 1);
    TF_CHECK_INT(g_mock.gpio_values[2], 1);
    tf_end();

    tf_begin("motion/流程 LOOP 变量次数");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "SET $N = 2\n"
                     "LOOP $N\n"
                     "GPIO 0 ON\n"
                     "ENDLOOP\n"),
                 0);
    motion_start();
    TF_CHECK_INT(run_to_terminal(20), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_write_count[0], 2);
    tf_end();

    tf_begin("motion/流程 无限循环与 BREAK");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "SET $I = 0\n"
                     "LOOP\n"
                     "SET $I = $I + 1\n"
                     "IF $I >= 3\n"
                     "BREAK\n"
                     "END\n"
                     "ENDLOOP\n"
                     "GPIO 3 ON\n"),
                 0);
    motion_start();
    TF_CHECK_INT(run_to_terminal(20), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[3], 1);
    tf_end();

    tf_begin("motion/流程 GOTO");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "SET $I = 0\n"
                     "LABEL AGAIN\n"
                     "SET $I = $I + 1\n"
                     "IF $I < 3\n"
                     "GOTO AGAIN\n"
                     "END\n"
                     "GPIO 4 ON\n"),
                 0);
    motion_start();
    TF_CHECK_INT(run_to_terminal(30), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_write_count[4], 1);
    tf_end();

    tf_begin("motion/流程 CALL 与 RETURN");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "CALL SUB\n"
                     "GPIO 1 ON\n"
                     "GOTO DONE\n"
                     "LABEL SUB\n"
                     "GPIO 0 ON\n"
                     "RETURN\n"
                     "LABEL DONE\n"),
                 0);
    motion_start();
    TF_CHECK_INT(run_to_terminal(10), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_write_count[0], 1);
    TF_CHECK_INT(g_mock.gpio_write_count[1], 1);
    tf_end();

    tf_begin("motion/流程 嵌套 IF 与 LOOP");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "SET $I = 0\n"
                     "LOOP 2\n"
                     "IF $I == 0\n"
                     "SET $I = 1\n"
                     "ELSE\n"
                     "SET $I = 2\n"
                     "END\n"
                     "ENDLOOP\n"
                     "IF $I == 2\n"
                     "GPIO 2 ON\n"
                     "END\n"),
                 0);
    motion_start();
    TF_CHECK_INT(run_to_terminal(30), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[2], 1);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 4. 设备指令                                                         */
/* ------------------------------------------------------------------ */

static void test_devices(void)
{
    tf_begin("motion/设备 STEP PWM GPIO ADC RELAY POSITION");
    reset_all();
    g_mock.gpio_values[1] = 1;
    g_mock.relay_values[1] = 1;
    g_mock.adc_value = 200;

    TF_CHECK_INT(motion_load(
                     "STEP 0 START, DIR 1, SPEED 1000rpm, PULSE 3200\n"
                     "STEP 0 STOP, DEC 100ms\n"
                     "PWM 0 START, FREQ 1000Hz, DUTY 50%, ACCEL 10ms\n"
                     "PWM 0 DUTY 80%, ACCEL 5ms\n"
                     "PWM 0 FREQ 2000Hz\n"
                     "PWM 0 STOP, DECEL 5ms\n"
                     "GPIO 0 ON\n"
                     "GPIO 0 OFF\n"
                     "GPIO 1 SET, VALUE 1\n"
                     "GPIO 1 READ, TO $G\n"
                     "ADC 0 READ, AVG 4, TO $A\n"
                     "RELAY 0 ON\n"
                     "RELAY 0 OFF\n"
                     "RELAY 1 READ, TO $R\n"
                     "STEP 0 POSITION SET 123\n"
                     "STEP 0 POSITION READ, TO $P\n"
                     "IF $G == 1 AND $A == 200 AND $R == 1 AND $P == 123\n"
                     "GPIO 2 ON\n"
                     "END\n"),
                 0);
    motion_start();
    TF_CHECK_INT(run_to_terminal(20), MOTION_STATE_COMPLETED);

    TF_CHECK_INT(g_mock.step_start_calls, 1);
    TF_CHECK_INT(g_mock.last_step_dir, 1);
    TF_CHECK_NEAR(g_mock.last_step_speed, 1000.0, 1e-9);
    TF_CHECK_INT(g_mock.last_step_unit, MOTION_UNIT_RPM);
    TF_CHECK_INT(g_mock.last_pulse, 3200);
    TF_CHECK_INT(g_mock.step_stop_calls, 1);
    TF_CHECK_INT(g_mock.last_dec_ms, 100);

    TF_CHECK_INT(g_mock.pwm_start_calls, 1);
    TF_CHECK_INT(g_mock.pwm_duty_calls, 1);
    TF_CHECK_INT(g_mock.pwm_freq_calls, 1);
    TF_CHECK_INT(g_mock.pwm_stop_calls, 1);
    TF_CHECK_INT(g_mock.last_pwm_duty, 80);
    TF_CHECK_INT(g_mock.last_pwm_freq, 2000);
    TF_CHECK_INT(g_mock.last_pwm_accel, 10);

    TF_CHECK_INT(g_mock.gpio_write_count[0], 2);
    TF_CHECK_INT(g_mock.gpio_values[0], 0);
    TF_CHECK_INT(g_mock.gpio_values[1], 1);
    TF_CHECK_INT(g_mock.gpio_values[2], 1);
    TF_CHECK_INT(g_mock.adc_read_calls, 4);
    TF_CHECK_INT(g_mock.relay_write_count[0], 2);
    TF_CHECK_INT(g_mock.relay_values[1], 1);
    TF_CHECK_INT(g_mock.step_position, 123);
    tf_end();

    tf_begin("motion/设备 STEP 使能/回零/速度/方向");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "STEP 0 ENABLE\n"
                     "STEP 0 DISABLE\n"
                     "STEP 0 SPEED 500rpm\n"
                     "STEP 0 DIR 0\n"
                     "STEP 0 HOME, SPEED 100rpm, DIR 1, TIMEOUT 30s\n"
                     "STEP 0 ZERO\n"),
                 0);
    motion_start();
    TF_CHECK_INT(run_to_terminal(10), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.step_enable_calls, 2);
    TF_CHECK_INT(g_mock.step_speed_calls, 1);
    TF_CHECK_INT(g_mock.step_dir_calls, 1);
    TF_CHECK_INT(g_mock.step_home_calls, 1);
    TF_CHECK_INT(g_mock.step_position, 0);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 5. WAIT                                                             */
/* ------------------------------------------------------------------ */

static void test_wait(void)
{
    tf_begin("motion/WAIT TIME");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "WAIT TIME 100ms\n"
                     "GPIO 0 ON\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    TF_CHECK_INT(g_mock.gpio_writes, 0);
    iot_test_tick_advance(50);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    iot_test_tick_advance(50);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();

    tf_begin("motion/WAIT STEP busy 到完成");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "STEP 0 START, DIR 1, SPEED 100rpm\n"
                     "WAIT STEP 0\n"
                     "GPIO 0 ON\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    g_mock.step_busy = 0;
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();

    tf_begin("motion/WAIT 超时错误");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "STEP 0 START, DIR 1, SPEED 100rpm\n"
                     "WAIT STEP 0, TIMEOUT 10ms\n"
                     "GPIO 0 ON\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    iot_test_tick_advance(11);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_TIMEOUT);
    TF_CHECK_INT(g_mock.gpio_writes, 0);
    tf_end();

    tf_begin("motion/WAIT ALL");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "STEP 0 START, DIR 1, SPEED 100rpm\n"
                     "WAIT ALL\n"
                     "GPIO 0 ON\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    g_mock.step_busy = 0;
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();

    tf_begin("motion/WAIT GPIO 条件");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "WAIT GPIO 0, VALUE 1, TIMEOUT 50ms\n"
                     "GPIO 1 ON\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    g_mock.gpio_values[0] = 1;
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[1], 1);
    tf_end();

    tf_begin("motion/WAIT PWM 与 ADC/SENSOR 条件");
    reset_all();
    g_mock.adc_value = 500;
    g_mock.sensor_value = 1;
    TF_CHECK_INT(motion_load(
                     "PWM 0 START, FREQ 100Hz, DUTY 10%\n"
                     "WAIT PWM 0\n"
                     "ADC 0 WAIT, GT 400\n"
                     "SENSOR 0 WAIT, VALUE 1\n"
                     "GPIO 0 ON\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    g_mock.pwm_busy = 0;
    TF_CHECK_INT(run_to_terminal(10), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 6. 错误处理                                                         */
/* ------------------------------------------------------------------ */

static void test_errors(void)
{
    tf_begin("motion/错误 未知指令");
    reset_all();
    TF_CHECK_INT(motion_load("FOO BAR\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_SYNTAX);
    tf_end();

    tf_begin("motion/错误 参数冲突");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "STEP 0 START, TIME 1s, PULSE 100\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_PARAMETER);
    TF_CHECK_INT(g_mock.step_start_calls, 0);
    tf_end();

    tf_begin("motion/错误 设备未注册");
    iot_test_reset_state();
    (void)motion_init();
    memset(&g_mock, 0, sizeof(g_mock));
    TF_CHECK_INT(motion_load(
                     "STEP 0 START, DIR 1, SPEED 100rpm\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_DEVICE);
    tf_end();

    tf_begin("motion/错误 ERROR 1008");
    reset_all();
    TF_CHECK_INT(motion_load("ERROR 1008\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, 1008);
    tf_end();

    tf_begin("motion/错误 ON ERROR 捕获");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "ON ERROR\n"
                     "GPIO 1 ON\n"
                     "ENDERROR\n"
                     "ERROR 1008\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, 1008);
    TF_CHECK_INT(g_mock.gpio_values[1], 1);
    tf_end();

    tf_begin("motion/错误 正常路径跳过 ON ERROR");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "ON ERROR\n"
                     "GPIO 1 ON\n"
                     "ENDERROR\n"
                     "GPIO 0 ON\n"),
                 0);
    motion_start();
    TF_CHECK_INT(run_to_terminal(10), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_write_count[1], 0);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 7. JOB 任务                                                         */
/* ------------------------------------------------------------------ */

static void test_jobs(void)
{
    tf_begin("motion/JOB 启动、等待与状态");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "JOB FAN\n"
                     "GPIO 0 ON\n"
                     "WAIT TIME 50ms\n"
                     "GPIO 0 OFF\n"
                     "ENDJOB\n"
                     "START JOB FAN\n"
                     "WAIT JOB FAN\n"
                     "JOB FAN STATE, TO $S\n"
                     "IF $S == 4\n"
                     "GPIO 2 ON\n"
                     "END\n"
                     "GPIO 1 ON\n"),
                 0);
    TF_CHECK_INT(motion_task_count(), 1);
    TF_CHECK(motion_task_find("FAN") >= 0);
    TF_CHECK_INT(motion_task_state("FAN"), MOTION_STATE_IDLE);

    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    TF_CHECK_INT(motion_task_state("FAN"), MOTION_STATE_WAITING);
    iot_test_tick_advance(50);
    motion_poll();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(motion_task_state("FAN"), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_write_count[0], 2);
    TF_CHECK_INT(g_mock.gpio_values[0], 0);
    TF_CHECK_INT(g_mock.gpio_values[1], 1);
    TF_CHECK_INT(g_mock.gpio_values[2], 1);
    tf_end();

    tf_begin("motion/JOB STOP 与资源释放");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "JOB MOTOR\n"
                     "STEP 0 START, DIR 1, SPEED 10rpm\n"
                     "LOOP\n"
                     "WAIT TIME 10ms\n"
                     "ENDLOOP\n"
                     "ENDJOB\n"),
                 0);
    TF_CHECK_INT(motion_task_start("MOTOR"), 0);
    TF_CHECK_INT(motion_task_state("MOTOR"), MOTION_STATE_RUNNING);
    TF_CHECK_INT(motion_task_start("MOTOR"), 0);
    TF_CHECK_INT(motion_task_stop("MOTOR"), 0);
    TF_CHECK_INT(motion_task_state("MOTOR"), MOTION_STATE_COMPLETED);
    tf_end();

    tf_begin("motion/JOB 设备资源冲突");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "JOB A\n"
                     "STEP 0 START, DIR 1, SPEED 10rpm\n"
                     "LOOP\n"
                     "WAIT TIME 10ms\n"
                     "ENDLOOP\n"
                     "ENDJOB\n"
                     "JOB B\n"
                     "STEP 0 START, DIR 0, SPEED 10rpm\n"
                     "ENDJOB\n"),
                 0);
    TF_CHECK_INT(motion_task_start("A"), 0);
    TF_CHECK_INT(motion_task_start("B"), MOTION_ERR_RESOURCE_BUSY);
    TF_CHECK_INT(motion_task_stop("A"), 0);
    TF_CHECK_INT(motion_task_start("B"), 0);
    TF_CHECK_INT(motion_task_stop("B"), 0);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 8. 边界条件                                                         */
/* ------------------------------------------------------------------ */

static void test_boundaries(void)
{
    tf_begin("motion/边界 空脚本与纯注释");
    reset_all();
    TF_CHECK_INT(motion_load(""), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);

    reset_all();
    TF_CHECK_INT(motion_load("; nothing\n// still nothing\n\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);
    tf_end();

    tf_begin("motion/边界 超长行加载失败");
    {
        char long_line[400];
        char script[420];

        memset(long_line, 'A', sizeof(long_line) - 1U);
        long_line[sizeof(long_line) - 1U] = '\0';
        snprintf(script, sizeof(script), "%s\n", long_line);
        reset_all();
        TF_CHECK(motion_load(script) != 0);
    }
    tf_end();

    tf_begin("motion/边界 IF 栈深度");
    reset_all();
    {
        char script[512];
        int i;
        size_t used = 0U;

        for (i = 0; i < 9; i++) {
            int n = snprintf(script + used, sizeof(script) - used,
                             "IF 1\n");
            if (n > 0) {
                used += (size_t)n;
            }
        }
        for (i = 0; i < 9; i++) {
            int n = snprintf(script + used, sizeof(script) - used,
                             "END\n");
            if (n > 0) {
                used += (size_t)n;
            }
        }
        TF_CHECK_INT(motion_load(script), 0);
        motion_start();
        motion_poll();
        TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
        TF_CHECK_INT(motion_get_error()->code,
                     MOTION_ERR_STACK_OVERFLOW);
    }
    tf_end();

    tf_begin("motion/边界 变量数量上限");
    reset_all();
    {
        char script[2048];
        int i;
        size_t used = 0U;

        for (i = 0; i < 65; i++) {
            int n = snprintf(script + used, sizeof(script) - used,
                             "SET $V%d = 1\n", i);
            if (n > 0) {
                used += (size_t)n;
            }
        }
        TF_CHECK_INT(motion_load(script), 0);
        motion_start();
        TF_CHECK_INT(run_to_terminal(30), MOTION_STATE_ERROR);
        TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_VARIABLE);
    }
    tf_end();

    tf_begin("motion/边界 CALL 深度与标签不存在");
    reset_all();
    TF_CHECK_INT(motion_load("LABEL L\nCALL L\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_STACK_OVERFLOW);

    reset_all();
    TF_CHECK_INT(motion_load("GOTO NOWHERE\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_FLOW);
    tf_end();

    tf_begin("motion/边界 暂停恢复与终止");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "WAIT TIME 100ms\n"
                     "GPIO 0 ON\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    TF_CHECK_INT(motion_pause(), 0);
    TF_CHECK_INT(motion_state(), MOTION_STATE_PAUSED);
    TF_CHECK_INT(motion_resume(), 0);
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    TF_CHECK_INT(motion_abort(), 0);
    TF_CHECK_INT(motion_state(), MOTION_STATE_ABORTED);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 9. 数据源                                                           */
/* ------------------------------------------------------------------ */

static void test_source(void)
{
    motion_source_t src;
    char line[64];
    uint32_t offset = 0U;

    tf_begin("motion/数据源 BOM 与逐行读取");
    motion_source_string_init(&src, "\xEF\xBB\xBFSTEP 0 ON\r\nGPIO 1 ON\n\nTAIL");
    TF_CHECK_INT(src.open(&src, NULL), 0);
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 1);
    TF_CHECK_STR(line, "STEP 0 ON");
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 1);
    TF_CHECK_STR(line, "GPIO 1 ON");
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 1);
    TF_CHECK_STR(line, "");
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 1);
    TF_CHECK_STR(line, "TAIL");
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 0);
    tf_end();

    tf_begin("motion/数据源 seek/tell 与异常参数");
    motion_source_string_init(&src, "AAA\nBBB\n");
    TF_CHECK_INT(src.open(&src, NULL), 0);
    TF_CHECK_INT(src.tell(&src, &offset), 0);
    TF_CHECK_INT((int)offset, 0);
    TF_CHECK_INT(src.seek(&src, 4U), 0);
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 1);
    TF_CHECK_STR(line, "BBB");
    TF_CHECK_INT(src.tell(&src, &offset), 0);
    TF_CHECK_INT((int)offset, 8);
    TF_CHECK_INT(src.seek(&src, 100U), -1);
    TF_CHECK_INT(src.seek(&src, 0U), 0);
    TF_CHECK_INT(src.read_line(&src, NULL, sizeof(line)), -1);
    TF_CHECK_INT(src.read_line(&src, line, 0U), -1);
    TF_CHECK_INT(src.tell(&src, NULL), -1);
    TF_CHECK_INT(src.close(&src), 0);
    tf_end();

    tf_begin("motion/数据源 超长行与空文本");
    {
        char text[320];

        memset(text, 'X', 299U);
        text[299] = '\n';
        memcpy(&text[300], "OK\n", 4U);
        motion_source_string_init(&src, text);
        TF_CHECK_INT(src.open(&src, NULL), 0);
        TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), -2);
        TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 1);
        TF_CHECK_STR(line, "OK");
    }
    motion_source_string_init(&src, NULL);
    TF_CHECK_INT(src.length, 0);
    TF_CHECK_INT(src.open(&src, NULL), -1);
    TF_CHECK_INT(src.read_line(&src, line, sizeof(line)), 0);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 10. 解析器补充                                                      */
/* ------------------------------------------------------------------ */

static void test_parser_extra(void)
{
    motion_instruction_t ins;
    const motion_param_t *p;
    motion_value_t v;

    tf_begin("motion/解析 值类型与数值缩放");
    TF_CHECK_INT(motion_parser_parse_value("42", &v), 0);
    TF_CHECK_INT(v.type, MOTION_VALUE_INTEGER);
    TF_CHECK_INT(v.data.integer, 42);
    TF_CHECK_INT(motion_parser_parse_value("-7", &v), 0);
    TF_CHECK_INT(v.data.integer, -7);
    TF_CHECK_INT(motion_parser_parse_value("3.5", &v), 0);
    TF_CHECK_INT(v.type, MOTION_VALUE_FLOAT);
    TF_CHECK_NEAR(v.data.real, 3.5, 1e-9);
    TF_CHECK_INT(motion_parser_parse_value("1.5s", &v), 0);
    TF_CHECK_INT(v.type, MOTION_VALUE_INTEGER);
    TF_CHECK_INT(v.data.integer, 1500);
    TF_CHECK_INT(v.unit, MOTION_UNIT_MS);
    TF_CHECK_INT(motion_parser_parse_value("2cm", &v), 0);
    TF_CHECK_INT(v.data.integer, 20);
    TF_CHECK_INT(v.unit, MOTION_UNIT_MM);
    TF_CHECK_INT(motion_parser_parse_value("3m", &v), 0);
    TF_CHECK_INT(v.data.integer, 3000);
    TF_CHECK_INT(motion_parser_parse_value("5kHz", &v), 0);
    TF_CHECK_INT(v.data.integer, 5000);
    TF_CHECK_INT(v.unit, MOTION_UNIT_HZ);
    TF_CHECK_INT(motion_parser_parse_value("TRUE", &v), 0);
    TF_CHECK_INT(v.type, MOTION_VALUE_BOOLEAN);
    TF_CHECK(v.data.boolean);
    TF_CHECK_INT(motion_parser_parse_value("OFF", &v), 0);
    TF_CHECK(!v.data.boolean);
    TF_CHECK_INT(motion_parser_parse_value("\"A;B//C\"", &v), 0);
    TF_CHECK_STR(v.data.string, "A;B//C");
    TF_CHECK_INT(motion_parser_parse_value("$VAR", &v), 0);
    TF_CHECK_INT(v.type, MOTION_VALUE_VARIABLE);
    TF_CHECK_STR(v.data.variable, "VAR");
    TF_CHECK(motion_parser_parse_value("10xyz", &v) != 0);
    TF_CHECK(motion_parser_parse_value("", &v) != 0);
    tf_end();

    tf_begin("motion/解析 默认单位与布尔参数");
    TF_CHECK_INT(motion_parser_parse(
                     "PWM 0 START, FREQ 2kHz, DUTY 25", &ins), MOTION_PARSE_OK);
    p = find_param(&ins, "FREQ");
    TF_CHECK(p != NULL);
    if (p != NULL) {
        TF_CHECK_INT(p->value.data.integer, 2000);
        TF_CHECK_INT(p->value.unit, MOTION_UNIT_HZ);
    }
    p = find_param(&ins, "DUTY");
    TF_CHECK(p != NULL);
    if (p != NULL) {
        TF_CHECK_INT(p->value.data.integer, 25);
        TF_CHECK_INT(p->value.unit, MOTION_UNIT_PERCENT);
    }
    TF_CHECK_INT(motion_parser_parse("STEP 0 SPEED 500rpm", &ins),
                 MOTION_PARSE_OK);
    p = find_param(&ins, "VALUE");
    TF_CHECK(p != NULL);
    if (p != NULL) {
        TF_CHECK_INT(p->value.data.integer, 500);
        TF_CHECK_INT(p->value.unit, MOTION_UNIT_RPM);
    }
    TF_CHECK_INT(motion_parser_parse("WAIT SENSOR 3, VALUE 1", &ins),
                 MOTION_PARSE_OK);
    TF_CHECK_INT(ins.device_id, 3);
    TF_CHECK_STR(ins.action, "SENSOR");
    TF_CHECK_INT(motion_parser_parse("GPIO 2 SET, VALUE ON", &ins),
                 MOTION_PARSE_OK);
    p = find_param(&ins, "VALUE");
    TF_CHECK(p != NULL);
    if (p != NULL) {
        TF_CHECK_INT(p->value.type, MOTION_VALUE_BOOLEAN);
        TF_CHECK(p->value.data.boolean);
    }
    tf_end();

    tf_begin("motion/解析 扩展非法输入");
    TF_CHECK_INT(motion_parser_parse("WAIT ADC 0", &ins),
                 MOTION_PARSE_INVALID);
    TF_CHECK_INT(motion_parser_parse("WAIT JOB", &ins),
                 MOTION_PARSE_INVALID);
    TF_CHECK_INT(motion_parser_parse("STEP 0 POSITION FOO", &ins),
                 MOTION_PARSE_INVALID);
    TF_CHECK_INT(motion_parser_parse("START FOO", &ins),
                 MOTION_PARSE_INVALID);
    TF_CHECK_INT(motion_parser_parse("IF", &ins), MOTION_PARSE_INVALID);
    TF_CHECK_INT(motion_parser_parse("SET $A =", &ins),
                 MOTION_PARSE_INVALID);
    TF_CHECK_INT(motion_parser_parse("ERROR ON", &ins),
                 MOTION_PARSE_INVALID);
    TF_CHECK_INT(motion_parser_parse("LABEL X Y", &ins),
                 MOTION_PARSE_INVALID);
    TF_CHECK_STR(motion_parse_result_message(MOTION_PARSE_OK), "ok");
    TF_CHECK_STR(motion_parse_result_message(MOTION_PARSE_EMPTY),
                 "empty line");
    TF_CHECK_STR(motion_parse_result_message(MOTION_PARSE_UNSUPPORTED),
                 "unsupported instruction");
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 11. 加载期校验                                                      */
/* ------------------------------------------------------------------ */

static void test_load_errors(void)
{
    static char big[MOTION_MAX_PROGRAM + 1U];

    tf_begin("motion/加载 参数与容量错误");
    reset_all();
    TF_CHECK_INT(motion_load(NULL), MOTION_ERR_PARAMETER);
    memset(big, 'A', sizeof(big) - 1U);
    big[sizeof(big) - 1U] = '\0';
    TF_CHECK_INT(motion_load(big), MOTION_ERR_MEMORY);
    tf_end();

    tf_begin("motion/加载 结构不匹配");
    reset_all();
    TF_CHECK_INT(motion_load("LABEL A\nLABEL A\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_load("END\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_load("ELSE\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_load("ENDLOOP\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_load("IF 1\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_load("LOOP 2\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_load("JOB X\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_load("ENDJOB\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_load("ON ERROR\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_load("ENDERROR\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_load("JOB A\nJOB B\nENDJOB\nENDJOB\n"),
                 MOTION_ERR_FLOW);
    tf_end();

    tf_begin("motion/加载 任务名冲突与数量上限");
    reset_all();
    TF_CHECK_INT(motion_load("JOB X\nENDJOB\nJOB X\nENDJOB\n"),
                 MOTION_ERR_PARAMETER);
    {
        char script[512];
        size_t used = 0U;
        int i;

        for (i = 0; i < 17; i++) {
            int n = snprintf(script + used, sizeof(script) - used,
                             "JOB J%d\nENDJOB\n", i);
            if (n > 0) {
                used += (size_t)n;
            }
        }
        TF_CHECK(used < sizeof(script));
        TF_CHECK_INT(motion_load(script), MOTION_ERR_FLOW);
    }
    tf_end();

    tf_begin("motion/加载 失败后状态与重新加载");
    reset_all();
    TF_CHECK_INT(motion_load("IF 1\n"), MOTION_ERR_FLOW);
    TF_CHECK_INT(motion_start(), MOTION_ERR_PARAMETER);
    TF_CHECK_INT(motion_state(), MOTION_STATE_IDLE);
    TF_CHECK_INT(motion_load("GPIO 0 ON\n"), 0);
    TF_CHECK_INT(motion_start(), 0);
    TF_CHECK_INT(run_to_terminal(10), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();

    tf_begin("motion/加载 清除上次任务");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "JOB F\n"
                     "GPIO 0 ON\n"
                     "ENDJOB\n"
                     "GPIO 1 ON\n"),
                 0);
    TF_CHECK_INT(motion_task_count(), 1);
    TF_CHECK_INT(motion_load("GPIO 2 ON\n"), 0);
    TF_CHECK_INT(motion_task_count(), 0);
    TF_CHECK_INT(motion_start(), 0);
    TF_CHECK_INT(run_to_terminal(10), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[2], 1);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 12. 运行期流程错误                                                  */
/* ------------------------------------------------------------------ */

static void expect_exec_error(const char *script, int code)
{
    reset_all();
    TF_CHECK_INT(motion_load(script), 0);
    TF_CHECK_INT(motion_start(), 0);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, code);
}

static void test_runtime_flow_errors(void)
{
    tf_begin("motion/运行期 流程结构错误");
    expect_exec_error("BREAK\n", MOTION_ERR_FLOW);
    expect_exec_error("RETURN\n", MOTION_ERR_FLOW);
    expect_exec_error("CALL NOPE\n", MOTION_ERR_FLOW);
    tf_end();

    tf_begin("motion/运行期 ERROR 消息与处理器重入");
    reset_all();
    TF_CHECK_INT(motion_load("ERROR \"CUSTOM\"\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_RUNTIME);
    TF_CHECK_STR(motion_get_error()->message, "CUSTOM");

    expect_exec_error("ON ERROR\nERROR 1009\nENDERROR\nERROR 1008\n", 1009);
    tf_end();

    tf_begin("motion/运行期 循环深度与零次循环");
    {
        char script[256];
        size_t used = 0U;
        int i;

        for (i = 0; i < 9; i++) {
            int n = snprintf(script + used, sizeof(script) - used,
                             "LOOP 1\n");
            if (n > 0) {
                used += (size_t)n;
            }
        }
        for (i = 0; i < 9; i++) {
            int n = snprintf(script + used, sizeof(script) - used,
                             "ENDLOOP\n");
            if (n > 0) {
                used += (size_t)n;
            }
        }
        reset_all();
        TF_CHECK_INT(motion_load(script), 0);
        motion_start();
        TF_CHECK_INT(run_to_terminal(40), MOTION_STATE_ERROR);
        TF_CHECK_INT(motion_get_error()->code,
                     MOTION_ERR_STACK_OVERFLOW);
    }
    reset_all();
    TF_CHECK_INT(motion_load(
                     "SET $I = 0\n"
                     "LOOP 0\n"
                     "SET $I = 1\n"
                     "ENDLOOP\n"
                     "IF $I == 0\n"
                     "GPIO 0 ON\n"
                     "END\n"),
                 0);
    motion_start();
    TF_CHECK_INT(run_to_terminal(20), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 13. 控制状态补充                                                    */
/* ------------------------------------------------------------------ */

static void test_control(void)
{
    tf_begin("motion/控制 未加载启动与重复运行");
    reset_all();
    TF_CHECK_INT(motion_start(), MOTION_ERR_PARAMETER);
    TF_CHECK_INT(motion_load("GPIO 0 ON\n"), 0);
    TF_CHECK_INT(motion_start(), 0);
    TF_CHECK_INT(run_to_terminal(10), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_write_count[0], 1);
    TF_CHECK_INT(motion_start(), 0);
    TF_CHECK_INT(run_to_terminal(10), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_write_count[0], 2);
    tf_end();

    tf_begin("motion/控制 暂停先于首次调度");
    reset_all();
    TF_CHECK_INT(motion_load("GPIO 0 ON\nGPIO 1 ON\n"), 0);
    TF_CHECK_INT(motion_start(), 0);
    TF_CHECK_INT(motion_pause(), 0);
    TF_CHECK_INT(motion_state(), MOTION_STATE_PAUSED);
    motion_poll();
    TF_CHECK_INT(g_mock.gpio_writes, 0);
    TF_CHECK_INT(motion_resume(), 0);
    TF_CHECK_INT(motion_state(), MOTION_STATE_RUNNING);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_writes, 2);
    tf_end();

    tf_begin("motion/控制 停止与终止");
    reset_all();
    TF_CHECK_INT(motion_load("WAIT TIME 100ms\nGPIO 0 ON\n"), 0);
    TF_CHECK_INT(motion_start(), 0);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    TF_CHECK_INT(motion_stop(), 0);
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);
    motion_poll();
    TF_CHECK_INT(g_mock.gpio_writes, 0);

    TF_CHECK_INT(motion_start(), 0);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    TF_CHECK_INT(motion_abort(), 0);
    TF_CHECK_INT(motion_state(), MOTION_STATE_ABORTED);
    motion_poll();
    TF_CHECK_INT(g_mock.gpio_writes, 0);
    tf_end();

    tf_begin("motion/控制 空闲时暂停恢复");
    reset_all();
    TF_CHECK_INT(motion_pause(), 0);
    TF_CHECK_INT(motion_state(), MOTION_STATE_IDLE);
    TF_CHECK_INT(motion_resume(), 0);
    TF_CHECK_INT(motion_state(), MOTION_STATE_IDLE);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_IDLE);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 14. 变量补充                                                        */
/* ------------------------------------------------------------------ */

static void test_variables_extra(void)
{
    motion_variable_table_t table;
    motion_value_t value;
    motion_value_t ref;
    motion_value_t out;

    tf_begin("motion/变量 清空与非法设置");
    motion_variable_init(&table);
    memset(&value, 0, sizeof(value));
    value.type = MOTION_VALUE_INTEGER;
    value.data.integer = 5;
    TF_CHECK_INT(motion_variable_set(&table, "A", &value), 0);
    TF_CHECK_INT(motion_variable_set(&table, "$a", &value), 0);
    TF_CHECK_INT(table.count, 1);
    TF_CHECK(motion_variable_exists(&table, "a"));
    motion_variable_clear(&table);
    TF_CHECK_INT(table.count, 0);
    TF_CHECK(!motion_variable_exists(&table, "A"));
    TF_CHECK_INT(motion_variable_set(&table, "", &value),
                 MOTION_ERR_VARIABLE);
    TF_CHECK_INT(motion_variable_set(NULL, "A", &value),
                 MOTION_ERR_VARIABLE);
    TF_CHECK_INT(motion_variable_set(&table, "A", NULL),
                 MOTION_ERR_VARIABLE);
    tf_end();

    tf_begin("motion/变量 引用解析与类型转换");
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
    TF_CHECK_STR(out.data.string, "12");
    TF_CHECK_INT(motion_value_to_int(&out), 12);
    TF_CHECK_NEAR(motion_value_to_double(&out), 12.0, 1e-9);
    TF_CHECK(motion_value_to_bool(&out));
    strcpy(ref.data.variable, "NOPE");
    TF_CHECK_INT(motion_value_resolve(&ref, &table, &out),
                 MOTION_ERR_VARIABLE);
    TF_CHECK_INT(motion_value_resolve(NULL, &table, &out),
                 MOTION_ERR_PARAMETER);
    memset(&out, 0, sizeof(out));
    out.type = MOTION_VALUE_INTEGER;
    out.data.integer = 0;
    TF_CHECK(!motion_value_to_bool(&out));
    TF_CHECK_INT(motion_value_to_int(NULL), 0);
    TF_CHECK_NEAR(motion_value_to_double(NULL), 0.0, 1e-9);
    TF_CHECK(!motion_value_to_bool(NULL));
    tf_end();

    tf_begin("motion/变量 字符串浮点布尔与表达式");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "SET $S = \"FEED\"\n"
                     "SET $F = 2.5\n"
                     "SET $B = TRUE\n"
                     "SET $N = -3\n"
                     "SET $X = 10\n"
                     "SET $X = $X + 5\n"
                     "IF $S == \"FEED\" AND $F > 2.4 AND $B AND $N < 0 AND "
                     "$X == 15\n"
                     "GPIO 0 ON\n"
                     "END\n"),
                 0);
    TF_CHECK_INT(motion_start(), 0);
    TF_CHECK_INT(run_to_terminal(10), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();

    tf_begin("motion/变量 作为指令参数与未定义错误");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "SET $SPD = 500\n"
                     "SET $DIR = 1\n"
                     "SET $T = 250\n"
                     "STEP 0 START, DIR $DIR, TIME $T, SPEED $SPD\n"),
                 0);
    TF_CHECK_INT(motion_start(), 0);
    TF_CHECK_INT(run_to_terminal(10), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.last_step_dir, 1);
    TF_CHECK_INT(g_mock.last_time_ms, 250);
    TF_CHECK_NEAR(g_mock.last_step_speed, 500.0, 1e-9);

    expect_exec_error("STEP 0 START, DIR $NOPE\n", MOTION_ERR_VARIABLE);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 15. 参数错误                                                        */
/* ------------------------------------------------------------------ */

static void test_params(void)
{
    tf_begin("motion/参数 设备指令缺失");
    expect_exec_error("STEP 0 SPEED\n", MOTION_ERR_PARAMETER);
    expect_exec_error("STEP 0 DIR\n", MOTION_ERR_PARAMETER);
    expect_exec_error("STEP 0 POSITION READ, TO 5\n", MOTION_ERR_PARAMETER);
    expect_exec_error("ADC 0 WAIT\n", MOTION_ERR_PARAMETER);
    expect_exec_error("SENSOR 0 WAIT\n", MOTION_ERR_PARAMETER);
    expect_exec_error("GPIO 0 READ, TO 5\n", MOTION_ERR_PARAMETER);
    expect_exec_error("ADC 0 READ, TO 5\n", MOTION_ERR_PARAMETER);
    tf_end();

    tf_begin("motion/参数 任务指令缺失");
    expect_exec_error("WAIT JOB NOPE\n", MOTION_ERR_PARAMETER);
    expect_exec_error("START JOB NOPE\n", MOTION_ERR_PARAMETER);
    expect_exec_error("STOP JOB NOPE\n", MOTION_ERR_PARAMETER);
    expect_exec_error("JOB NOPE STATE, TO $S\n", MOTION_ERR_PARAMETER);
    expect_exec_error(
        "JOB X\nGPIO 0 ON\nENDJOB\nJOB X STATE\n", MOTION_ERR_PARAMETER);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 16. WAIT 补充                                                       */
/* ------------------------------------------------------------------ */

static void test_wait_extra(void)
{
    tf_begin("motion/WAIT TIME 0 立即完成");
    reset_all();
    TF_CHECK_INT(motion_load("WAIT TIME 0ms\nGPIO 0 ON\n"), 0);
    TF_CHECK_INT(motion_start(), 0);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();

    tf_begin("motion/WAIT SENSOR 条件等待");
    reset_all();
    g_mock.sensor_value = 0;
    TF_CHECK_INT(motion_load(
                     "WAIT SENSOR 0, VALUE 1, TIMEOUT 50ms\n"
                     "GPIO 0 ON\n"),
                 0);
    TF_CHECK_INT(motion_start(), 0);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    g_mock.sensor_value = 1;
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();

    tf_begin("motion/WAIT TIMEOUT 0 无限等待");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "STEP 0 START, DIR 1, SPEED 10rpm\n"
                     "WAIT STEP 0, TIMEOUT 0\n"
                     "GPIO 0 ON\n"),
                 0);
    TF_CHECK_INT(motion_start(), 0);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    iot_test_tick_advance(10000U);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    g_mock.step_busy = 0;
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();

    tf_begin("motion/WAIT ALL 超时");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "STEP 0 START, DIR 1, SPEED 10rpm\n"
                     "WAIT ALL, TIMEOUT 5ms\n"),
                 0);
    TF_CHECK_INT(motion_start(), 0);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    iot_test_tick_advance(6U);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_TIMEOUT);
    tf_end();

    tf_begin("motion/WAIT GPIO 变量目标值");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "SET $V = 1\n"
                     "WAIT GPIO 0, VALUE $V, TIMEOUT 50ms\n"
                     "GPIO 1 ON\n"),
                 0);
    TF_CHECK_INT(motion_start(), 0);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    g_mock.gpio_values[0] = 1;
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[1], 1);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 17. JOB 补充                                                        */
/* ------------------------------------------------------------------ */

static void test_jobs_extra(void)
{
    tf_begin("motion/JOB 多任务并行");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "JOB A\n"
                     "GPIO 0 ON\n"
                     "ENDJOB\n"
                     "JOB B\n"
                     "GPIO 1 ON\n"
                     "ENDJOB\n"
                     "START JOB A\n"
                     "START JOB B\n"
                     "WAIT JOB A\n"
                     "WAIT JOB B\n"
                     "GPIO 2 ON\n"),
                 0);
    TF_CHECK_INT(motion_start(), 0);
    TF_CHECK_INT(run_to_terminal(50), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(motion_task_state("A"), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(motion_task_state("B"), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    TF_CHECK_INT(g_mock.gpio_values[1], 1);
    TF_CHECK_INT(g_mock.gpio_values[2], 1);
    tf_end();

    tf_begin("motion/JOB 完成后重启与名称大小写");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "JOB Feed\n"
                     "GPIO 0 ON\n"
                     "ENDJOB\n"),
                 0);
    TF_CHECK(motion_task_find("feed") >= 0);
    TF_CHECK_INT(motion_task_start("feed"), 0);
    motion_poll();
    TF_CHECK_INT(motion_task_state("FEED"), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(g_mock.gpio_write_count[0], 1);
    TF_CHECK_INT(motion_task_start("Feed"), 0);
    motion_poll();
    TF_CHECK_INT(g_mock.gpio_write_count[0], 2);
    TF_CHECK_INT(motion_task_start("nope"), MOTION_ERR_PARAMETER);
    TF_CHECK_INT(motion_task_stop("nope"), MOTION_ERR_PARAMETER);
    TF_CHECK_INT(motion_task_find("nope"), -1);
    tf_end();

    tf_begin("motion/JOB 出错隔离与资源释放");
    reset_all();
    TF_CHECK_INT(motion_load(
                     "JOB BAD\n"
                     "STEP 0 START, DIR 1, SPEED 10rpm\n"
                     "ERROR 1006\n"
                     "ENDJOB\n"
                     "START JOB BAD\n"
                     "WAIT JOB BAD\n"
                     "GPIO 0 ON\n"),
                 0);
    TF_CHECK_INT(motion_start(), 0);
    TF_CHECK_INT(run_to_terminal(30), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(motion_task_state("BAD"), MOTION_STATE_ERROR);
    TF_CHECK_INT(g_mock.gpio_values[0], 1);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 18. 设备失败传播                                                    */
/* ------------------------------------------------------------------ */

static void test_device_failures(void)
{
    tf_begin("motion/设备 注册与空实现");
    motion_device_reset();
    TF_CHECK(motion_device_get() == NULL);
    TF_CHECK_INT(motion_device_register(NULL), MOTION_ERR_PARAMETER);
    TF_CHECK_INT(motion_device_gpio_write(0U, 1U), MOTION_ERR_DEVICE);
    TF_CHECK_INT(motion_device_adc_read(0U, NULL), MOTION_ERR_DEVICE);
    {
        bool busy = true;

        TF_CHECK_INT(motion_device_any_busy(NULL), MOTION_ERR_PARAMETER);
        TF_CHECK_INT(motion_device_any_busy(&busy), 0);
        TF_CHECK(!busy);
    }
    reset_all();
    TF_CHECK(motion_device_get() != NULL);
    tf_end();

    tf_begin("motion/设备 执行失败传播");
    reset_all();
    g_mock.fail_gpio_write = 1;
    TF_CHECK_INT(motion_load("GPIO 0 ON\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_DEVICE_EXEC);

    reset_all();
    g_mock.fail_step_start = 1;
    TF_CHECK_INT(motion_load(
                     "STEP 0 START, DIR 1, SPEED 10rpm\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_DEVICE_EXEC);

    reset_all();
    g_mock.fail_pwm_start = 1;
    TF_CHECK_INT(motion_load(
                     "PWM 0 START, FREQ 100Hz, DUTY 10%\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_DEVICE_EXEC);

    reset_all();
    g_mock.fail_pwm_duty = 1;
    TF_CHECK_INT(motion_load("PWM 0 START, DUTY 10%\nPWM 0 DUTY 50\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_DEVICE_EXEC);

    reset_all();
    g_mock.fail_adc_read = 1;
    TF_CHECK_INT(motion_load("ADC 0 READ\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_DEVICE_EXEC);

    reset_all();
    g_mock.fail_sensor_read = 1;
    TF_CHECK_INT(motion_load("SENSOR 0 READ\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_DEVICE_EXEC);

    reset_all();
    g_mock.fail_relay_write = 1;
    TF_CHECK_INT(motion_load("RELAY 0 ON\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_DEVICE_EXEC);

    reset_all();
    g_mock.fail_gpio_read = 1;
    TF_CHECK_INT(motion_load("GPIO 0 READ\n"), 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_DEVICE_EXEC);
    tf_end();

    tf_begin("motion/设备 忙碌查询失败传播");
    reset_all();
    g_mock.fail_step_busy = 1;
    TF_CHECK_INT(motion_load(
                     "STEP 0 START, DIR 1, SPEED 10rpm\n"
                     "WAIT STEP 0\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_ERROR);
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_DEVICE_EXEC);

    reset_all();
    g_mock.fail_pwm_busy = 1;
    TF_CHECK_INT(motion_load(
                     "PWM 0 START, FREQ 100Hz, DUTY 10%\n"
                     "WAIT PWM 0\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    motion_poll();
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_DEVICE_EXEC);

    reset_all();
    g_mock.fail_step_busy = 1;
    TF_CHECK_INT(motion_load(
                     "STEP 0 START, DIR 1, SPEED 10rpm\n"
                     "WAIT ALL\n"),
                 0);
    motion_start();
    motion_poll();
    TF_CHECK_INT(motion_state(), MOTION_STATE_WAITING);
    motion_poll();
    TF_CHECK_INT(motion_get_error()->code, MOTION_ERR_DEVICE_EXEC);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 19. 流程索引查询                                                    */
/* ------------------------------------------------------------------ */

static uint32_t offset_of(const char *text, const char *needle)
{
    const char *p = strstr(text, needle);

    return (p != NULL) ? (uint32_t)(p - text) : 0U;
}

static void test_flow_query(void)
{
    static const char script[] =
        "LABEL START\n"
        "IF 1\n"
        "GPIO 0 ON\n"
        "ELSE\n"
        "GPIO 1 ON\n"
        "END\n"
        "LOOP 2\n"
        "GPIO 2 ON\n"
        "ENDLOOP\n"
        "JOB FAN\n"
        "GPIO 3 ON\n"
        "ENDJOB\n";
    const motion_label_t *label;
    const motion_if_entry_t *if_entry;
    const motion_loop_entry_t *loop;
    const motion_job_entry_t *job;
    uint32_t if_off = offset_of(script, "IF 1");
    uint32_t else_off = offset_of(script, "ELSE");
    uint32_t end_off = offset_of(script, "END\n");
    uint32_t loop_off = offset_of(script, "LOOP 2");
    uint32_t body_off = offset_of(script, "GPIO 2 ON");
    uint32_t endloop_off = offset_of(script, "ENDLOOP");

    tf_begin("motion/流程索引 标签条件循环与任务");
    reset_all();
    TF_CHECK_INT(motion_load(script), 0);
    label = motion_flow_find_label("START", 0);
    TF_CHECK(label != NULL);
    if (label != NULL) {
        TF_CHECK_INT((int)label->offset, 0);
        TF_CHECK_INT((int)label->next_offset, (int)if_off);
        TF_CHECK_INT((int)motion_flow_line_at(label->offset), 1);
    }
    TF_CHECK(motion_flow_find_label("START", 1) == label);
    TF_CHECK(motion_flow_find_label("NOPE", 0) == NULL);
    TF_CHECK(motion_flow_find_label(NULL, 0) == NULL);

    if_entry = motion_flow_find_if(if_off);
    TF_CHECK(if_entry != NULL);
    if (if_entry != NULL) {
        TF_CHECK(if_entry->has_else);
        TF_CHECK_INT((int)if_entry->else_offset, (int)else_off);
        TF_CHECK_INT((int)if_entry->end_offset, (int)end_off);
    }
    TF_CHECK(motion_flow_find_if(else_off) == NULL);
    TF_CHECK(motion_flow_find_else(else_off) == if_entry);
    TF_CHECK(motion_flow_find_error(if_off) == NULL);
    TF_CHECK(motion_flow_find_handler(0) == NULL);

    loop = motion_flow_find_loop(loop_off);
    TF_CHECK(loop != NULL);
    if (loop != NULL) {
        TF_CHECK_INT((int)loop->body_offset, (int)body_off);
        TF_CHECK_INT((int)loop->endloop_offset, (int)endloop_off);
    }
    TF_CHECK(motion_flow_find_loop(0U) == NULL);
    TF_CHECK_INT((int)motion_flow_line_at(loop_off), 7);

    job = motion_flow_find_job("FAN");
    TF_CHECK(job != NULL);
    TF_CHECK_INT((int)motion_flow_job_count(), 1);
    TF_CHECK(motion_flow_job_at(0) == job);
    TF_CHECK(motion_flow_job_at(1) == NULL);
    if (job != NULL) {
        TF_CHECK(motion_flow_find_job_by_offset(job->job_offset) == job);
        TF_CHECK_INT((int)motion_flow_line_at(job->job_offset), 10);
    }
    TF_CHECK(motion_flow_find_job_by_offset(0U) == NULL);
    TF_CHECK(motion_flow_find_job("NOPE") == NULL);
    TF_CHECK_INT((int)motion_flow_line_at(0xFFFFU), 0);
    tf_end();

    tf_begin("motion/流程索引 重建与错误");
    TF_CHECK_INT(motion_flow_build(NULL), 0);
    TF_CHECK_INT((int)motion_flow_job_count(), 0);
    TF_CHECK_INT(motion_flow_build("IF 1\n"), MOTION_ERR_FLOW);
    tf_end();
}

/* ------------------------------------------------------------------ */
/* 20. 错误工具                                                        */
/* ------------------------------------------------------------------ */

static void test_error_utils(void)
{
    motion_error_t err;

    tf_begin("motion/错误 工具函数");
    motion_error_clear(&err);
    TF_CHECK_INT(err.code, MOTION_ERR_NONE);
    motion_error_set(&err, MOTION_ERR_TIMEOUT, 12U, "wait timeout");
    TF_CHECK_INT(err.code, MOTION_ERR_TIMEOUT);
    TF_CHECK_INT((int)err.line, 12);
    TF_CHECK_STR(err.message, "wait timeout");
    motion_error_set(&err, MOTION_ERR_FLOW, 3U, NULL);
    TF_CHECK_STR(err.message,
                 motion_error_message(MOTION_ERR_FLOW));
    motion_error_setf(&err, MOTION_ERR_DEVICE, 4U, "dev %d fail", 7);
    TF_CHECK_STR(err.message, "dev 7 fail");
    TF_CHECK_STR(motion_error_message(MOTION_ERR_NONE), "no error");
    TF_CHECK_STR(motion_error_message(9999), "unknown error");
    tf_end();
}

/* ------------------------------------------------------------------ */

void ut_motion(void)
{
    test_parser();
    test_parser_extra();
    test_variables();
    test_variables_extra();
    test_flow();
    test_flow_query();
    test_devices();
    test_device_failures();
    test_wait();
    test_wait_extra();
    test_errors();
    test_runtime_flow_errors();
    test_params();
    test_jobs();
    test_jobs_extra();
    test_control();
    test_source();
    test_load_errors();
    test_error_utils();
    test_boundaries();
}
