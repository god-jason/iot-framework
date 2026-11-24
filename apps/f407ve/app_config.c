/*
 * app_config.c - STM32F407VET6 应用配置持久化（内部 Flash）
 *
 * 职责：
 *   - 定义配置结构体与默认值；
 *   - 启动时从 S2/S3 加载（无记录/校验失败/长度不符则用默认值）；
 *   - boot_count +1 并保存，复位后可见计数增长（验证掉电保持）；
 *   - 把配置交给 components/config 的通用 cfg 命令（config_cmd.c）。
 *
 * cfg 命令的具体实现（show/save/reset/format/test）见
 * components/config/config_cmd.c，本文件只提供描述符与回调。
 */

#include "app_config.h"

#include "boot_config.h"
#include "config.h"
#include "config_cmd.h"
#include "log.h"

#include <string.h>

#define APP_CONFIG_DEFAULT_THRESHOLD    25.0f

static app_config_t s_cfg;
/* cfg test 工作缓冲：前 sizeof(s_cfg) 工作副本，后 sizeof(s_cfg) 读回缓冲 */
static uint8_t s_test_buf[sizeof(app_config_t) * 2U];
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

/* cfg test 第 round 轮的测试内容 */
static void app_cfg_fill_test(void *data, uint32_t round)
{
    app_config_t *cfg = (app_config_t *)data;

    cfg->boot_count = 0x10000U + round;
    cfg->device_id = (uint16_t)(0x10U + (round & 0x0FU));
    cfg->mode = (uint8_t)(round & 0x07U);
    cfg->threshold = 10.0f + (float)round;
}

int app_config_init(void)
{
    int rc;

    if (boot_config_init() != 0) {
        LOG_E("cfg", "storage init failed");
        return -1;
    }

    app_cfg_defaults(&s_cfg);
    rc = boot_config_load(&s_cfg, sizeof(s_cfg));
    if (rc == 0) {
        if (s_cfg.magic != APP_CONFIG_MAGIC) {
            LOG_W("cfg", "magic mismatch 0x%X, use defaults",
                  (unsigned)s_cfg.magic);
            app_cfg_defaults(&s_cfg);
        } else {
            LOG_I("cfg", "loaded:");
            app_cfg_show(&s_cfg);
        }
    } else if (rc == -2) {
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
    if (boot_config_save(&s_cfg, sizeof(s_cfg)) != 0) {
        LOG_E("cfg", "save failed");
        return -1;
    }
    return 0;
}

void app_config_register(void)
{
    config_cmd_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.store = boot_config_storage();
    desc.data = &s_cfg;
    desc.size = (uint32_t)sizeof(s_cfg);
    desc.scratch = s_test_buf;
    desc.set_defaults = app_cfg_defaults;
    desc.show = app_cfg_show;
    desc.fill_test = app_cfg_fill_test;
    (void)config_cmd_register(&desc);
}
