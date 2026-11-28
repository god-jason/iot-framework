/*
 * boot_meta.h - STM32 启动状态（F1/F4 共用，存于 KV）
 *
 * 只保存启动失败计数，OTA 信息不落盘：
 *   BOOT_KV_KEY_STATE : boot_state_t  启动状态（tries / crash）
 *
 * 启动流程：
 *   Bootloader 每次启动 tries+1 后启动 App；App 初始化完成调用
 *   boot_confirm() 把 tries/crash 清零。tries 超过 BOOT_MAX_TRIES 时
 *   停留在 Bootloader 等待串口 OTA。
 *
 * OTA 升级：
 *   OTA 区自描述（包头见 components/fota/fota.h），Bootloader 每次启动
 *   直接检测包头魔术字/头 CRC/长度，有效则升级；升级成功后擦除 OTA 区，
 *   中断则包头仍在，下次启动重新升级（无进度、无续传）。
 *
 * 本头文件不依赖芯片头：Flash 擦写由 platforms/stm32<family>/board_flash.c 实现。
 */

#ifndef BOOT_META_H
#define BOOT_META_H

#include "partition.h"

#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 * 启动状态
 * ========================================================================== */

#define BOOT_STATE_MAGIC        0x54415453U     /* 'STAT' */
#define BOOT_STATE_SIZE         32U

#define BOOT_MAX_TRIES          5U              /* 连续启动失败上限 */
#define BOOT_MAX_CRASH          5U              /* 连续崩溃上限 */

/* 启动状态：作为 KV 的 BOOT_KV_KEY_STATE 存储 */
typedef struct {
    uint32_t magic;         /* BOOT_STATE_MAGIC */
    uint32_t tries;         /* 启动次数，App 启动成功后清零 */
    uint32_t crash;         /* 崩溃次数，App 启动成功后清零 */
    uint32_t reserved[4];   /* 保留（写 0） */
    uint32_t crc;           /* 以上字段的 CRC32 */
} boot_state_t;

typedef char boot_state_size_check[(sizeof(boot_state_t) == BOOT_STATE_SIZE) ? 1 : -1];

/* ==========================================================================
 * 启动状态（Bootloader / App 共用）
 * ========================================================================== */

bool boot_state_valid(const boot_state_t *s);
void boot_state_load(boot_state_t *s);      /* 无有效状态时返回默认值 */
int boot_state_save(boot_state_t *s);

/* App 启动确认：tries/crash 清零 */
void boot_confirm(void);

#endif /* BOOT_META_H */
