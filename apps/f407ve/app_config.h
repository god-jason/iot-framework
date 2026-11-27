#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*
 * app_config.h - STM32F407VET6 应用配置持久化（内部 Flash）
 *
 * 配置结构体由 cfg.h 的 X 宏描述（字段与默认值同表维护），以原始内存
 * 形式存放在 KV 区（BOOT_KV_KEY_CONFIG），由 components/config 的
 * config_* 接口读写，并注册为 "app_config_t"：
 *   cfg get app_config_t
 *   cfg set app_config_t {"threshold":30}
 * 即可用 JSON 查询 / 修改整份配置（缺失字段回落默认值）。
 *
 * 字段变化时递增 APP_CONFIG_VERSION；长度不符时回退默认值。
 */

#include <stdint.h>

#include "cfg.h"

#define APP_CONFIG_MAGIC    0x41434647U     /* 'ACFG' */
#define APP_CONFIG_VERSION  3U              /* 配置结构体版本（v3：扁平化 + cfg 映射） */

/* 配置字段表：X(类型, 成员, 默认值[, string 容量]) */
#define APP_CONFIG_CFG(X) \
    X(uint32,  magic,      APP_CONFIG_MAGIC) \
    X(uint32,  boot_count, 0) \
    X(uint16,  device_id,  1) \
    X(uint8,   mode,       0) \
    X(uint8,   reserved,   0) \
    X(float32, threshold,  25.0f) \
    X(string,  mqtt_host,         "47.114.32.128", 32) \
    X(uint16,  mqtt_port,         1883) \
    X(string,  mqtt_device_id,    "abe3b1849d344789924e75adb249", 40) \
    X(string,  mqtt_model_id,     "36e65ed9b72b4610acaa45b2561a", 40) \
    X(string,  mqtt_model_secret, "123456", 32) \
    X(uint16,  mqtt_keepalive_s,  60) \
    X(bool,    mqtt_clean_session, true)

CFG_DEFINE(app_config_t, APP_CONFIG_CFG)

/* 加载配置（无有效记录时使用默认值）并记录本次启动；返回 0 成功 */
int app_config_init(void);

/* 当前内存中的配置（app_config_init 之后有效） */
const app_config_t *app_config_get(void);

/* 保存当前配置（追加一条 KV 记录）；返回 0 成功 */
int app_config_save(void);

#endif /* APP_CONFIG_H */
