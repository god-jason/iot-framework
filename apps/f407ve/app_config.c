/*
 * app_config.c - STM32F407VET6 应用配置持久化（KV 区）
 *
 * 职责：
 *   - 定义配置结构体与默认值；
 *   - 启动时从 KV 区加载（无记录/校验失败/长度不符则用默认值）；
 *   - boot_count +1 并保存，复位后可见计数增长（验证掉电保持）。
 *
 * 存储：components/config 直接调用 components/kvfs（双区滚动 + Commit），
 * 键为 BOOT_KV_KEY_CONFIG；config 为单例，初始化一次后直接读写。
 */

#include "app_config.h"

#include "boot_kv.h"
#include "config.h"
#include "log.h"

#include <string.h>

#define APP_CONFIG_DEFAULT_THRESHOLD    25.0f

static app_config_t s_cfg;
static uint8_t s_ready;

static void app_cfg_defaults(void *data)
{
    app_config_t *cfg = (app_config_t *)data;

    memset(cfg, 0, sizeof(*cfg));
    cfg->magic = APP_CONFIG_MAGIC;
    cfg->device_id = 1U;
    cfg->mode = 0U;
    cfg->threshold = APP_CONFIG_DEFAULT_THRESHOLD;
}

/* 打印当前配置（浮点按千分位整数输出，避免依赖 %f） */
static void app_cfg_show(const void *data)
{
    const app_config_t *cfg = (const app_config_t *)data;
    int whole = (int)cfg->threshold;
    int frac = (int)((cfg->threshold - (float)whole) * 1000.0f);

    LOG_I("cfg", "id=%u mode=%u thr=%d.%03d boot=%u",
          (unsigned)cfg->device_id, (unsigned)cfg->mode, whole, frac,
          (unsigned)cfg->boot_count);
}

int app_config_init(void)
{
    int rc;

    if (boot_kv_init() != 0) {
        LOG_E("cfg", "kv init failed");
        return -1;
    }
    (void)config_init(boot_kv(), BOOT_KV_KEY_CONFIG,
                      APP_CONFIG_VERSION, (uint32_t)sizeof(s_cfg));

    app_cfg_defaults(&s_cfg);
    rc = config_load(&s_cfg, sizeof(s_cfg));
    if (rc == CONFIG_OK) {
        if (s_cfg.magic != APP_CONFIG_MAGIC) {
            LOG_W("cfg", "magic mismatch 0x%X, use defaults",
                  (unsigned)s_cfg.magic);
            app_cfg_defaults(&s_cfg);
        } else {
            LOG_I("cfg", "loaded:");
            app_cfg_show(&s_cfg);
        }
    } else if (rc == CONFIG_NONE) {
        LOG_I("cfg", "no config, use defaults");
    } else {
        /* 长度不匹配（结构体升级）或校验失败：回退默认值 */
        LOG_W("cfg", "load failed (%d), use defaults", rc);
        app_cfg_defaults(&s_cfg);
    }

    s_cfg.boot_count++;
    s_ready = 1U;
    return app_config_save();
}

const app_config_t *app_config_get(void)
{
    return &s_cfg;
}

int app_config_save(void)
{
    if (s_ready == 0U) {
        return -1;
    }
    s_cfg.magic = APP_CONFIG_MAGIC;
    if (config_save(&s_cfg, sizeof(s_cfg)) != CONFIG_OK) {
        LOG_E("cfg", "save failed");
        return -1;
    }
    return 0;
}
