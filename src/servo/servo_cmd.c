/*
 * servo_cmd.c - servo 组件 roshell 命令（servo <id> [angle]）
 *
 * 实例由 servo_init() 调用 servo_cmd_add() 登记；首次登记时把命令
 * 注册到 roshell 全局命令表。
 */

#include "servo_cmd.h"

#include "ros.h"
#include "common.h"
#include "roshell.h"

#define SERVO_CMD_MAX 8U

static servo_t *s_servos[SERVO_CMD_MAX];
static uint8_t s_servo_count;
static bool s_servo_cmd_ready;

static int servo_cmd_index(const servo_t *servo)
{
    uint8_t i;

    for (i = 0U; i < s_servo_count; i++) {
        if (s_servos[i] == servo) {
            return (int)i;
        }
    }
    return -1;
}

static int servo_cmd_handler(roshell_t *rs, int argc, char **argv,
                             const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)user;
    uint32_t id;
    uint32_t angle = 0U;
    servo_t *servo;

    if (!rs_args_get_uint(parsed, 0U, &id)) {
        ros_puts("usage: servo <id> [angle]");
        return -1;
    }
    if (id >= s_servo_count || s_servos[id] == NULL) {
        ros_printf("servo %u not found\n", (unsigned)id);
        return -1;
    }
    servo = s_servos[id];

    if (!rs_args_get_uint(parsed, 1U, &angle)) {
        LOG_I("servo", "servo[%u] angle=%u range=%u..%u", (unsigned)id,
              (unsigned)servo->angle, (unsigned)servo->min_angle,
              (unsigned)servo->max_angle);
        return 0;
    }
    if (servo_set_angle(servo, (uint16_t)angle) != 0) {
        ros_printf("servo[%u] set angle failed\n", (unsigned)id);
        return -1;
    }
    return 0;
}

static const rs_args_t s_servo_args[] = {
    { "id", 0, RS_ARGS_TYPE_UINT, true, true, NULL, "servo instance id" },
    { "angle", 0, RS_ARGS_TYPE_UINT, true, false, NULL, "target angle" }
};

static const rs_cmd_t s_servo_def = {
    "servo", "servo angle control", NULL, NULL, s_servo_args, 2U,
    servo_cmd_handler, NULL
};

void servo_cmd_add(servo_t *servo)
{
    if (servo_cmd_index(servo) >= 0 || s_servo_count >= SERVO_CMD_MAX) {
        return;
    }
    s_servos[s_servo_count++] = servo;
    if (!s_servo_cmd_ready) {
        s_servo_cmd_ready = true;
        (void)rs_cmd_register(&s_servo_def);
    }
}
