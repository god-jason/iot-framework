/*
 * config_cmd.c - 配置存储通用 Shell 命令（cfg）
 *
 * 与具体配置结构体解耦：所有操作都通过 config_cmd_desc_t 提供的
 * 存储句柄 / 配置内存 / 回调完成，板级只需注册一次。
 *
 * cfg show            打印当前配置与 Flash 状态
 * cfg save            追加保存当前配置
 * cfg reset           恢复默认值并保存
 * cfg format          擦除两区并写入默认值
 * cfg test [n]        连续写入 n 条并逐条回读比较，结束后恢复原配置
 */

#include "config_cmd.h"

#include "config.h"
#include "log.h"
#include "ros.h"
#include "roshell.h"

#include <string.h>

#define CFG_CMD_ROUNDS          8U
#define CFG_CMD_MAX_ROUNDS      2000U

static config_cmd_desc_t s_desc;
static uint8_t s_registered;

/* 打印底层存储状态：序号、活动扇区、追加偏移、有效记录数 */
static void cfg_cmd_log_flash(void)
{
    const config_store_t *store = s_desc.store;

    LOG_I("cfg", "flash seq=%u sector=%u offset=%u records=%u",
          (unsigned)store->sequence, (unsigned)store->active_index,
          (unsigned)store->write_offset, (unsigned)store->record_count);
}

/* 保存并打印结果；what 为成功后的说明 */
static int cfg_cmd_save(const char *what)
{
    int rc = config_store_save(s_desc.store, s_desc.data, s_desc.size);

    if (rc != CONFIG_STORE_OK) {
        LOG_E("cfg", "save failed rc=%d", rc);
        return -1;
    }
    LOG_I("cfg", "%s", what);
    if (s_desc.show != NULL) {
        s_desc.show(s_desc.data);
    }
    cfg_cmd_log_flash();
    return 0;
}

/*
 * 写读一致性测试：
 *   scratch 前 size 字节作为工作副本，后 size 字节作为读回缓冲；
 *   每轮 fill_test 工作副本 → 保存 → 回读比较，最后把测试前的配置写回。
 */
static int cfg_cmd_test(uint32_t rounds)
{
    config_store_t *store = s_desc.store;
    uint8_t *work = (uint8_t *)s_desc.scratch;
    uint8_t *readback;
    uint32_t sector;
    uint32_t rotations = 0U;
    uint32_t t0 = ros_tick_get();
    uint32_t i;
    int fail_at = -1;

    if (work == NULL || s_desc.size == 0U) {
        LOG_E("cfg", "test buffer not configured");
        return -1;
    }
    readback = work + s_desc.size;
    sector = store->active_index;
    if (rounds == 0U) {
        rounds = 1U;
    }
    if (rounds > CFG_CMD_MAX_ROUNDS) {
        rounds = CFG_CMD_MAX_ROUNDS;
    }

    memcpy(work, s_desc.data, s_desc.size);
    for (i = 0U; i < rounds; i++) {
        if (s_desc.fill_test != NULL) {
            s_desc.fill_test(work, i);
        }
        if (config_store_save(store, work, s_desc.size) != CONFIG_STORE_OK) {
            fail_at = (int)i;
            break;
        }
        if (store->active_index != sector) {
            rotations++;
            sector = store->active_index;
        }
        memset(readback, 0, s_desc.size);
        if (config_store_load(store, readback, s_desc.size) != CONFIG_STORE_OK ||
            memcmp(readback, work, s_desc.size) != 0) {
            fail_at = (int)i;
            break;
        }
    }

    /* 测试写入了临时内容，恢复为测试前的配置 */
    if (config_store_save(store, s_desc.data, s_desc.size) != CONFIG_STORE_OK) {
        LOG_E("cfg", "restore after test failed");
    }

    if (fail_at >= 0) {
        LOG_E("cfg", "selftest FAIL at round %d/%u", fail_at, (unsigned)rounds);
        cfg_cmd_log_flash();
        return -1;
    }
    LOG_I("cfg", "selftest OK: %u rounds, rotations=%u, cost=%u ms",
          (unsigned)rounds, (unsigned)rotations,
          (unsigned)(ros_tick_get() - t0));
    cfg_cmd_log_flash();
    return 0;
}

static int cfg_cmd_run(const rs_cmd_ctx_t *ctx)
{
    const char *action = NULL;
    uint32_t rounds = CFG_CMD_ROUNDS;

    if (s_registered == 0U) {
        ros_puts("cfg: command not registered");
        return -1;
    }
    (void)rs_args_get_str(ctx->parsed, 0U, &action);
    (void)rs_args_get_uint(ctx->parsed, 1U, &rounds);
    if (action == NULL || action[0] == '\0') {
        action = "show";
    }

    if (strcmp(action, "show") == 0) {
        if (s_desc.show != NULL) {
            s_desc.show(s_desc.data);
        }
        cfg_cmd_log_flash();
        return 0;
    }
    if (strcmp(action, "save") == 0) {
        return cfg_cmd_save("saved");
    }
    if (strcmp(action, "reset") == 0) {
        if (s_desc.set_defaults == NULL) {
            LOG_E("cfg", "no defaults callback");
            return -1;
        }
        s_desc.set_defaults(s_desc.data);
        return cfg_cmd_save("reset to defaults");
    }
    if (strcmp(action, "format") == 0) {
        if (config_store_format(s_desc.store) != CONFIG_STORE_OK) {
            LOG_E("cfg", "format failed");
            return -1;
        }
        if (s_desc.set_defaults != NULL) {
            s_desc.set_defaults(s_desc.data);
        }
        return cfg_cmd_save("formatted, defaults written");
    }
    if (strcmp(action, "test") == 0) {
        return cfg_cmd_test(rounds);
    }

    ros_puts("cfg: unknown action, use show|save|reset|format|test [n]");
    return -1;
}

static const rs_args_t s_cfg_cmd_args[] = {
    { "action", 0, RS_ARGS_TYPE_STR, true, false, "show",
      "show|save|reset|format|test" },
    { "n", 0, RS_ARGS_TYPE_UINT, true, false, "8", "test rounds" }
};

static const rs_cmd_t s_cfg_cmd_def = {
    "cfg", "config in flash (show/save/reset/format/test)", NULL, NULL,
    s_cfg_cmd_args, 2U, cfg_cmd_run, NULL, NULL, NULL, NULL
};

int config_cmd_register(const config_cmd_desc_t *desc)
{
    if (desc == NULL || desc->store == NULL ||
        desc->data == NULL || desc->size == 0U) {
        return -1;
    }
    s_desc = *desc;
    s_registered = 1U;
    return rs_cmd_register(&s_cfg_cmd_def);
}
