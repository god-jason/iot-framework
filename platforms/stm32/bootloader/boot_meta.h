/*
 * boot_meta.h - STM32 启动状态与升级进度（F1/F4 共用，存于 KV）
 *
 * 启动状态与 OTA 进度都保存在 KV 区（components/fkv）：
 *   BOOT_KV_KEY_STATE : boot_state_t  启动状态（tries / crash / ota 标识）
 *   BOOT_KV_KEY_OTA   : uint32_t      OTA 拷贝进度（已写入 App 的字节数）
 *
 * 启动流程：
 *   Bootloader 每次启动 tries+1 后启动 App；App 初始化完成调用
 *   boot_confirm() 把 tries/crash 清零。tries 超过 BOOT_MAX_TRIES 时
 *   停留在 Bootloader 等待串口 OTA。
 *
 * OTA 升级：
 *   OTA 区保存原始编译结果 bin（无包头），长度/CRC 存在 boot_state 里；
 *   Bootloader 分块把 OTA 区拷入 App 区，每拷完一块更新一次进度；
 *   掉电后按最后进度继续（F4 按扇区只能整区重来）；
 *   全部完成并校验 CRC 通过后清除标识与进度。
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
    uint32_t ota_valid;     /* 1=OTA 区有完整升级包，需要校验并拷贝到 App */
    uint32_t ota_len;       /* 升级包长度 */
    uint32_t ota_crc;       /* 升级包 CRC32 */
    uint32_t reserved;      /* 保留（写 0） */
    uint32_t crc;           /* 以上字段的 CRC32 */
} boot_state_t;

typedef char boot_state_size_check[(sizeof(boot_state_t) == BOOT_STATE_SIZE) ? 1 : -1];

/* ==========================================================================
 * 启动状态 / 升级进度（Bootloader / App 共用）
 * ========================================================================== */

bool boot_state_valid(const boot_state_t *s);
void boot_state_load(boot_state_t *s);      /* 无有效状态时返回默认值 */
int boot_state_save(boot_state_t *s);       /* 写入 KV 并清除 OTA 进度 */

bool boot_history_last_length(uint32_t *length);  /* 无进度返回 false */
int boot_history_append(const boot_state_t *s, uint32_t length);  /* 更新进度 */

/* App 启动确认：tries/crash 清零 */
void boot_confirm(void);

#endif /* BOOT_META_H */
