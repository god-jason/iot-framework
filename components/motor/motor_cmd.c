/*
 * motor_cmd.c - motor 组件 roshell 命令（motor <id> speed|dir|stop|info）
 *
 * 实例由 motor_init() 调用 motor_cmd_add() 登记；首次登记时把命令
 * 注册到 roshell 全局命令表。
 */

#include "motor_cmd.h"

#include "ros.h"
#include "common.h"
#include "roshell.h"

#define MOTOR_CMD_MAX 8U

static motor_t *s_motors[MOTOR_CMD_MAX];
static uint8_t s_motor_count;
static bool s_motor_cmd_ready;

static int motor_cmd_index(const motor_t *motor)
{
    uint8_t i;

    for (i = 0U; i < s_motor_count; i++) {
        if (s_motors[i] == motor) {
            return (int)i;
        }
    }
    return -1;
}

static int motor_cmd_handler(roshell_t *rs, int argc, char **argv,
                             const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)user;
    uint32_t id;
    const char *action = NULL;
    const char *value = NULL;
    motor_t *motor;
    int32_t v = 0;
    int rc;

    if (!rs_args_get_uint(parsed, 0U, &id) || !rs_args_get_str(parsed, 1U, &action)) {
        ros_puts("usage: motor <id> speed <n> | dir <0-3> | stop | info");
        return -1;
    }
    if (id >= s_motor_count || s_motors[id] == NULL) {
        ros_printf("motor %u not found\n", (unsigned)id);
        return -1;
    }
    motor = s_motors[id];
    (void)rs_args_get_str(parsed, 2U, &value);

    if (str_icmp(action, "speed")) {
        if (value == NULL || !str_parse_int(value, &v)) {
            ros_puts("usage: motor <id> speed <n>");
            return -1;
        }
        rc = motor_set_speed(motor, v);
    } else if (str_icmp(action, "dir")) {
        if (value == NULL || !str_parse_int(value, &v) ||
            v < MOTOR_STOPPED || v > MOTOR_BRAKING) {
            ros_puts("usage: motor <id> dir <0-3>");
            return -1;
        }
        rc = motor_set_direction(motor, (motor_direction_e)v);
    } else if (str_icmp(action, "stop")) {
        rc = motor_stop(motor);
    } else if (str_icmp(action, "info")) {
        LOG_I("motor", "motor[%u] speed=%ld max=%ld dir=%d", (unsigned)id,
              (long)motor->speed, (long)motor->max_speed,
              (int)motor->direction);
        return 0;
    } else {
        ros_printf("motor: unknown action '%s'\n", action);
        return -1;
    }

    if (rc != 0) {
        ros_printf("motor[%u] %s failed\n", (unsigned)id, action);
        return -1;
    }
    return 0;
}

static const rs_args_t s_motor_args[] = {
    { "id", 0, RS_ARGS_TYPE_UINT, true, true, NULL, "motor instance id" },
    { "action", 0, RS_ARGS_TYPE_STR, true, true, NULL, "speed|dir|stop|info" },
    { "value", 0, RS_ARGS_TYPE_STR, true, false, NULL, "speed or direction" }
};

static const rs_cmd_t s_motor_def = {
    "motor", "motor control", NULL, NULL, s_motor_args, 3U, motor_cmd_handler, NULL
};

void motor_cmd_add(motor_t *motor)
{
    if (motor_cmd_index(motor) >= 0 || s_motor_count >= MOTOR_CMD_MAX) {
        return;
    }
    s_motors[s_motor_count++] = motor;
    if (!s_motor_cmd_ready) {
        s_motor_cmd_ready = true;
        (void)rs_cmd_register(&s_motor_def);
    }
}
