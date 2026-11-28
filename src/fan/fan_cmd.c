/*
 * fan_cmd.c - fan 组件 roshell 命令（fan <id> [duty]）
 *
 * 实例由 fan_init() 调用 fan_cmd_add() 登记；首次登记时把命令
 * 注册到 roshell 全局命令表。
 */

#include "fan_cmd.h"

#include "ros.h"
#include "common.h"
#include "roshell.h"

#define FAN_CMD_MAX 8U

static fan_t *s_fans[FAN_CMD_MAX];
static uint8_t s_fan_count;
static bool s_fan_cmd_ready;

static int fan_cmd_index(const fan_t *fan)
{
    uint8_t i;

    for (i = 0U; i < s_fan_count; i++) {
        if (s_fans[i] == fan) {
            return (int)i;
        }
    }
    return -1;
}

static int fan_cmd_handler(roshell_t *rs, int argc, char **argv,
                           const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)user;
    uint32_t id;
    uint32_t duty = 0U;
    fan_t *fan;

    if (!rs_args_get_uint(parsed, 0U, &id)) {
        ros_puts("usage: fan <id> [duty]");
        return -1;
    }
    if (id >= s_fan_count || s_fans[id] == NULL) {
        ros_printf("fan %u not found\n", (unsigned)id);
        return -1;
    }
    fan = s_fans[id];

    if (!rs_args_get_uint(parsed, 1U, &duty)) {
        ros_printf("fan[%u] duty=%u rpm=%u\n", (unsigned)id,
                   (unsigned)fan->duty, (unsigned)fan->rpm);
        return 0;
    }
    if (fan_set_speed(fan, (uint16_t)duty) != 0) {
        ros_printf("fan[%u] set duty failed\n", (unsigned)id);
        return -1;
    }
    ros_printf("fan[%u] duty=%u rpm=%u\n", (unsigned)id,
               (unsigned)fan->duty, (unsigned)fan->rpm);
    return 0;
}

static const rs_args_t s_fan_args[] = {
    { "id", 0, RS_ARGS_TYPE_UINT, true, true, NULL, "fan instance id" },
    { "duty", 0, RS_ARGS_TYPE_UINT, true, false, NULL, "duty 0..1000" }
};

static const rs_cmd_t s_fan_def = {
    "fan", "fan duty / rpm", NULL, NULL, s_fan_args, 2U, fan_cmd_handler, NULL,
    NULL, NULL, NULL
};

void fan_cmd_add(fan_t *fan)
{
    if (fan_cmd_index(fan) >= 0 || s_fan_count >= FAN_CMD_MAX) {
        return;
    }
    s_fans[s_fan_count++] = fan;
    if (!s_fan_cmd_ready) {
        s_fan_cmd_ready = true;
        (void)rs_cmd_register(&s_fan_def);
    }
}
