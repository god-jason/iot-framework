/*
 * boot_kv.h - Bootloader/App 共享的 KV 存储（components/fkv）
 *
 * KV 分区（A/B 两个等大区）地址与大小见各板 partition.h：
 *   FLASH_KV_BASE0 / FLASH_KV_BASE1 / FLASH_KV_SIZE
 *
 * 存放内容（key 固定，value 为结构体原始内存）：
 *   BOOT_KV_KEY_STATE  : boot_state_t   启动状态（tries/crash/ota 标识）
 *   BOOT_KV_KEY_OTA    : uint32_t       OTA 拷贝进度（已写入 App 的字节数）
 *   BOOT_KV_KEY_CONFIG : 应用配置结构体（如 app_config_t，含平台连接参数）
 *
 * 所有接口在首次调用时自动初始化并挂载；
 * 若 KV 区未格式化或版本不符，会自动格式化（会清空已有 KV 数据）。
 */

#ifndef BOOT_KV_H
#define BOOT_KV_H

#include "partition.h"
#include "fkv.h"

#include <stdint.h>

/* 单条 value 上限：需覆盖 boot_state_t 与应用配置结构体（含平台连接参数） */
#define BOOT_KV_MAX_VALUE       192U

/* KV 键值（'BST1' / 'OTA1' / 'CFG1'） */
#define BOOT_KV_KEY_STATE       0x42535431U
#define BOOT_KV_KEY_OTA         0x4F544131U
#define BOOT_KV_KEY_CONFIG      0x43464731U

/* 实例指针（供诊断/统计使用，boot_kv_init 之后有效） */
fkv_t *boot_kv(void);

/* 初始化 + 挂载（幂等）；KV 区无效时自动格式化。返回 0 成功 */
int boot_kv_init(void);

/* 写入 / 读取 / 删除（返回 0 成功，-1 失败） */
int boot_kv_put(uint32_t key, const void *data, uint32_t len);

/*
 * 读取：0 成功；-2 键不存在；-3 缓冲区不足（*len 输出实际长度）；
 *       -1 其它错误。
 */
int boot_kv_get(uint32_t key, void *buf, uint32_t cap, uint32_t *len);

int boot_kv_del(uint32_t key);

/* 是否存在：1=存在，0=不存在，-1=错误 */
int boot_kv_exists(uint32_t key);

#endif /* BOOT_KV_H */
