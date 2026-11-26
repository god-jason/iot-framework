/*
 * stepper_cmd.c - stepper 组件 roshell 命令
 *   stepper <id> dir|move|moveto|pos|info [value]
 *
 * 实例由 stepper_init() 调用 stepper_cmd_add() 登记；首次登记时把命令
 * 注册到 roshell 全局命令表。
 */

#include "stepper_cmd.h"

#include "ros.h"
#include "common.h"
#include "roshell.h"

#define STEPPER_CMD_MAX 8U

static stepper_t *s_steppers[STEPPER_CMD_MAX];
static uint8_t s_stepper_count;
static bool s_stepper_cmd_ready;

static int stepper_cmd_index(const stepper_t *st)
{
    uint8_t i;

    for (i = 0U; i < s_stepper_count; i++) {
        if (s_steppers[i] == st) {
            return (int)i;
        }
    }
    return -1;
}

static int stepper_cmd_handler(roshell_t *rs, int argc, char **argv,
                               const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)user;
    uint32_t id;
    const char *action = NULL;
    const char *value = NULL;
    stepper_t *st;
    int32_t v = 0;
    int rc;

    if (!rs_args_get_uint(parsed, 0U, &id) || !rs_args_get_str(parsed, 1U, &action)) {
        ros_puts("usage: stepper <id> dir|move|moveto|pos|info [value]");
        return -1;
    }
    if (id >= s_stepper_count || s_steppers[id] == NULL) {
        ros_printf("stepper %u not found\n", (unsigned)id);
        return -1;
    }
    st = s_steppers[id];
    (void)rs_args_get_str(parsed, 2U, &value);

    if (str_icmp(action, "pos")) {
        int32_t pos = 0;

        (void)stepper_get_position(st, &pos);
        ros_printf("stepper[%u] pos=%ld\n", (unsigned)id, (long)pos);
        return 0;
    }
    if (str_icmp(action, "info")) {
        ros_printf("stepper[%u] pos=%ld dir=%u\n", (unsigned)id,
                   (long)st->position, (unsigned)st->direction);
        return 0;
    }
    if (value == NULL || !str_parse_int(value, &v)) {
        ros_puts("usage: stepper <id> dir|move|moveto <value>");
        return -1;
    }
    if (str_icmp(action, "dir")) {
        rc = stepper_set_direction(st, (uint8_t)v);
    } else if (str_icmp(action, "move")) {
        rc = stepper_move(st, v);
    } else if (str_icmp(action, "moveto")) {
        rc = stepper_move_to(st, v);
    } else {
        ros_printf("stepper: unknown action '%s'\n", action);
        return -1;
    }

    if (rc != 0) {
        ros_printf("stepper[%u] %s failed\n", (unsigned)id, action);
        return -1;
    }
    return 0;
}

static const rs_args_t s_stepper_args[] = {
    { "id", 0, RS_ARGS_TYPE_UINT, true, true, NULL, "stepper instance id" },
    { "action", 0, RS_ARGS_TYPE_STR, true, true, NULL,
      "dir|move|moveto|pos|info" },
    { "value", 0, RS_ARGS_TYPE_STR, true, false, NULL, "steps / direction" }
};

static const rs_cmd_t s_stepper_def = {
    "stepper", "stepper motion control", NULL, NULL, s_stepper_args, 3U,
    stepper_cmd_handler, NULL
};

void stepper_cmd_add(stepper_t *st)
{
    if (stepper_cmd_index(st) >= 0 || s_stepper_count >= STEPPER_CMD_MAX) {
        return;
    }
    s_steppers[s_stepper_count++] = st;
    if (!s_stepper_cmd_ready) {
        s_stepper_cmd_ready = true;
        (void)rs_cmd_register(&s_stepper_def);
    }
}
