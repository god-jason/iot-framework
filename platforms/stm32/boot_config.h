/*
 * boot_config.h - STM32 配置存储（F1/F4 共用）
 *
 * 底层采用 components/config 的双区轮转追加日志：
 *   - 配置区两段（BOOT_CONFIG_BASE0/1，各 BOOT_CONFIG_SIZE）轮流追加；
 *   - 每条记录带 magic/version/length/sequence/CRC32，Commit 最后写入；
 *   - 掉电只会丢弃最后一条未提交记录，上一次完整配置仍可恢复。
 * 地址与大小见各板 partition.h。
 */

#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include "partition.h"
#include "config.h"

#include <stdbool.h>
#include <stdint.h>

/* 写入记录的配置结构体版本号（结构体变更时递增，配合 load_raw 做迁移） */
#define BOOT_CONFIG_VERSION     1U

/* 初始化（幂等）：扫描配置区并定位最新有效记录。返回 0 成功，-1 失败 */
int boot_config_init(void);

/* 读取最新配置：size 必须与记录中的数据长度一致。返回 0/-2(无配置)/-1 */
int boot_config_load(void *data, uint32_t size);

/*
 * 按缓冲区容量读取原始数据，并输出实际长度与版本号，
 * 供结构体升级时按 version/length 做迁移。返回 0/-2(无配置)/-1。
 */
int boot_config_load_raw(void *buf, uint32_t cap, uint32_t *len,
                         uint16_t *version);

/* 保存配置（追加写；当前区写满时自动轮转）。返回 0 成功，-1 失败 */
int boot_config_save(const void *data, uint32_t len);

/* 是否存在有效配置 */
bool boot_config_valid(void);

/* 擦除两区配置 */
int boot_config_format(void);

/* Bootloader 兼容接口：0=存在有效配置，-2=无有效配置，-1=初始化失败 */
int boot_config_restore(void);

/* 通用存储句柄：板级可直接调用 config_store_* 接口 */
config_store_t *boot_config_storage(void);

#endif /* BOOT_CONFIG_H */
