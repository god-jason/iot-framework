/*
 * encoder_cmd.c - encoder 组件 roshell 命令
 *   encoder <id> read | reset | speed <dt_ms> | info
 *
 * 实例由 encoder_init() 调用 encoder_cmd_add() 登记；首次登记时把命令
 * 注册到 roshell 全局命令表。
 */

#include "encoder_cmd.h"

#include "ros.h"
#include "common.h"
#include "roshell.h"

#define ENCODER_CMD_MAX 8U

static encoder_t *s_encoders[ENCODER_CMD_MAX];
static uint8_t s_encoder_count;
static bool s_encoder_cmd_ready;

static int encoder_cmd_index(const encoder_t *enc)
{
    uint8_t i;

    for (i = 0U; i < s_encoder_count; i++) {
        if (s_encoders[i] == enc) {
            return (int)i;
        }
    }
    return -1;
}

static int encoder_cmd_handler(roshell_t *rs, int argc, char **argv,
                               const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)user;
    uint32_t id;
    const char *action = NULL;
    const char *value = NULL;
    encoder_t *enc;

    if (!rs_args_get_uint(parsed, 0U, &id) || !rs_args_get_str(parsed, 1U, &action)) {
        ros_puts("usage: encoder <id> read | reset | speed <dt_ms> | info");
        return -1;
    }
    if (id >= s_encoder_count || s_encoders[id] == NULL) {
        ros_printf("encoder %u not found\n", (unsigned)id);
        return -1;
    }
    enc = s_encoders[id];
    (void)rs_args_get_str(parsed, 2U, &value);

    if (str_icmp(action, "read")) {
        int32_t count = 0;

        if (encoder_read(enc, &count) != 0) {
            ros_printf("encoder[%u] read failed\n", (unsigned)id);
            return -1;
        }
        LOG_I("encoder", "encoder[%u] count=%ld", (unsigned)id, (long)count);
        return 0;
    }
    if (str_icmp(action, "reset")) {
        if (encoder_reset(enc) != 0) {
            ros_printf("encoder[%u] reset failed\n", (unsigned)id);
            return -1;
        }
        return 0;
    }
    if (str_icmp(action, "speed")) {
        uint32_t dt_ms = 0U;
        float speed = 0.0f;

        if (value == NULL || !str_parse_uint(value, &dt_ms) || dt_ms == 0U) {
            ros_puts("usage: encoder <id> speed <dt_ms>");
            return -1;
        }
        if (encoder_get_speed(enc, (float)dt_ms / 1000.0f, &speed) != 0) {
            ros_printf("encoder[%u] speed failed\n", (unsigned)id);
            return -1;
        }
        LOG_I("encoder", "encoder[%u] speed=%ld pulse/s", (unsigned)id,
              (long)speed);
        return 0;
    }
    if (str_icmp(action, "info")) {
        LOG_I("encoder", "encoder[%u] count=%ld speed=%ld pulse/s", (unsigned)id,
              (long)enc->count, (long)enc->speed);
        return 0;
    }

    ros_printf("encoder: unknown action '%s'\n", action);
    return -1;
}

static const rs_args_t s_encoder_args[] = {
    { "id", 0, RS_ARGS_TYPE_UINT, true, true, NULL, "encoder instance id" },
    { "action", 0, RS_ARGS_TYPE_STR, true, true, NULL, "read|reset|speed|info" },
    { "value", 0, RS_ARGS_TYPE_STR, true, false, NULL, "speed: dt_ms" }
};

static const rs_cmd_t s_encoder_def = {
    "encoder", "encoder read/reset/speed", NULL, NULL, s_encoder_args, 3U,
    encoder_cmd_handler, NULL
};

void encoder_cmd_add(encoder_t *enc)
{
    if (encoder_cmd_index(enc) >= 0 || s_encoder_count >= ENCODER_CMD_MAX) {
        return;
    }
    s_encoders[s_encoder_count++] = enc;
    if (!s_encoder_cmd_ready) {
        s_encoder_cmd_ready = true;
        (void)rs_cmd_register(&s_encoder_def);
    }
}
