#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*
 * app_config.h - STM32F407VET6 应用配置持久化（内部 Flash）
 *
 * 配置以结构体原始内存形式存放在 KV 区（components/kvfs，A/B 双区滚动），
 * 由 components/config 的 config_* 单例接口读写（KV 键 BOOT_KV_KEY_CONFIG）。
 * 本文件只描述配置内容并提供默认值/打印。
 *
 * 结构体字段变化时必须同步递增 APP_CONFIG_VERSION，并通过
 * config_load_raw() 的 version/length 做迁移（本 Demo 简化处理：
 * 长度不匹配时回退默认值）。
 */

#include <stdint.h>

#define APP_CONFIG_MAGIC    0x41434647U     /* 'ACFG' */
#define APP_CONFIG_VERSION  1U              /* 配置结构体版本 */

typedef struct {
    uint32_t magic;         /* 必须为 APP_CONFIG_MAGIC */
    uint32_t boot_count;    /* 上电次数：每次启动 +1，用于验证掉电保持 */
    uint16_t device_id;     /* 演示参数 */
    uint8_t  mode;          /* 演示参数 */
    uint8_t  reserved;      /* 对齐/预留，必须写 0 */
    float    threshold;     /* 演示参数 */
} app_config_t;

/* 加载配置（无有效记录时使用默认值）并记录本次启动；返回 0 成功 */
int app_config_init(void);

/* 当前内存中的配置（app_config_init 之后有效） */
const app_config_t *app_config_get(void);

/* 保存当前配置（追加一条 KV 记录）；返回 0 成功 */
int app_config_save(void);

#endif /* APP_CONFIG_H */
