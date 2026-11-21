/*
 * boot_meta.h - A/B OTA 启动元数据、Flash 操作与启动确认（Bootloader / App 共用）
 *
 * 实现位于 boot_meta.c，由 Bootloader 与 App 各自链接一份：
 *   - Bootloader：读写元数据、校验镜像、擦写目标槽；
 *   - App：boot_confirm() 上报启动成功、boot_crash_count() 记录崩溃次数。
 */

#ifndef ROS_BOOT_META_H
#define ROS_BOOT_META_H

#include "stm32f10x.h"

#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 * 分区与元数据定义
 * ========================================================================== */

#define BOOT_SLOT_BASE0     0x08002000U     /* A 区起始 */
#define BOOT_SLOT_BASE1     0x08010000U     /* B 区起始 */
#define BOOT_SLOT_SIZE      (56U * 1024U)   /* 单槽大小 */
#define BOOT_META_PAGE0     0x0801E000U     /* 元数据页 0 */
#define BOOT_META_PAGE1     0x0801E400U     /* 元数据页 1 */

#define BOOT_FLASH_PAGE     1024U           /* F103 中容量：1KB/页 */
#define BOOT_META_MAGIC     0x42534F52U     /* 'ROSB'，元数据结构有效标识 */
#define BOOT_SLOT_NONE      0xFFFFFFFFU     /* pending = 无待确认槽 */
#define BOOT_MAX_TRIES      3U              /* 新槽最多尝试启动次数 */
#define BOOT_MAX_CRASH      5U              /* 崩溃次数上限，超过则回退分区 */

/*
 * 启动元数据：保存在两个独立的 1KB 页中，seq 较大的有效副本优先。
 * 结构按 4 字节对齐，长度 44 字节，写入时按半字（2 字节）编程。
 */
typedef struct {
    uint32_t magic;         /* BOOT_META_MAGIC */
    uint32_t seq;           /* 每次保存自增，用于选择最新副本 */
    uint32_t active;        /* 当前运行槽：0=A，1=B */
    uint32_t pending;       /* 待确认槽：0/1，BOOT_SLOT_NONE 表示无 */
    uint32_t tries;         /* pending 槽已连续启动的次数 */
    uint32_t crash;         /* 连续崩溃次数（App HardFault 累加，确认后清零） */
    uint32_t len[2];        /* 各槽镜像长度；0 表示未通过 OTA 写入 */
    uint32_t crc[2];        /* 各槽镜像 CRC32（仅 OTA 写入时有效） */
    uint32_t meta_crc;      /* 前面所有字段的 CRC32 */
} boot_meta_t;

/* ==========================================================================
 * CRC32 与 Flash
 * ========================================================================== */

uint32_t boot_crc32_update(uint32_t crc, const void *data, uint32_t len);
uint32_t boot_crc32(const void *data, uint32_t len);

int boot_flash_erase(uint32_t addr, uint32_t len);
int boot_flash_write(uint32_t addr, const void *data, uint32_t len);

/* ==========================================================================
 * 元数据（Bootloader / App 共用）
 * ========================================================================== */

bool boot_meta_valid(const boot_meta_t *m);
void boot_meta_load(boot_meta_t *m);
int boot_meta_save(boot_meta_t *m);

/*
 * App 启动确认：初始化完成后调用一次。
 *   - 有 pending：active 切换到 pending 槽，清除 pending / tries / crash；
 *   - 普通启动：清零 tries 与 crash。
 */
void boot_confirm(void);

/*
 * 记录一次崩溃（HardFault 中调用）：crash 自增并写回元数据。
 * Bootloader 下次启动时若 crash > BOOT_MAX_CRASH，会回退到另一个分区。
 */
void boot_crash_count(void);

#endif /* ROS_BOOT_META_H */
