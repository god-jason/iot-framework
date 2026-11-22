/*
 * boot_meta.h - STM32F407VE 启动状态、升级历史、Flash 与配置区
 *
 * Flash 布局（512KB，F407 扇区：4x16K + 1x64K + 3x128K，详见 partition.h）：
 *   1 Bootloader  0x08000000 16K（S0）
 *   2 Boot Meta   0x08004000 16K（S1，启动状态 + 追加升级历史）
 *   3 配置主区    0x08008000 16K（S2）
 *   4 配置备份    0x0800C000 16K（S3）
 *   5 错误日志    0x08010000 64K（S4，文本追加，带 Unix 时间）
 *   6 APP 主程序  0x08020000 128K（S5）
 *   7 BACKUP 备份 0x08040000 128K（S6）
 *   8 OTA 升级包  0x08060000 128K（S7）
 *
 * Boot Meta 分区布局：
 *   [0x000] boot_state_t        32B  启动状态
 *   [0x020] boot_ota_history_t  16B × N  升级进度历史（追加写）
 *
 * 启动失败（tries/crash 超过 5 次）时用备份区恢复 App 区；
 * App 启动成功调用 boot_confirm() 清零 tries/crash。
 * ota 区、备份区均为原始 bin，无包头，长度/CRC 保存在 boot_state 中。
 */

#ifndef ROS_BOOT_META_H
#define ROS_BOOT_META_H

#include "stm32f4xx.h"
#include "partition.h"

#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 * 启动状态与升级历史
 * ========================================================================== */

#define BOOT_STATE_MAGIC        0x54415453U     /* 'STAT' */
#define BOOT_HISTORY_MAGIC      0x54534948U     /* 'HIST' */

#define BOOT_STATE_SIZE         32U
#define BOOT_HISTORY_SLOT       16U
#define BOOT_HISTORY_BASE       (BOOT_META_BASE + BOOT_STATE_SIZE)
#define BOOT_HISTORY_COUNT      ((BOOT_META_SIZE - BOOT_STATE_SIZE) / BOOT_HISTORY_SLOT)

#define BOOT_MAX_TRIES          5U              /* 连续启动失败上限 */
#define BOOT_MAX_CRASH          5U              /* 连续崩溃上限 */

/* boot_state_t.reserved 位定义 */
#define BOOT_STATE_FLAG_BACKUP  0x00000001U     /* 备份区有效 */

typedef struct {
    uint32_t magic;         /* BOOT_STATE_MAGIC */
    uint32_t tries;         /* 启动次数，App 启动成功后清零 */
    uint32_t crash;         /* 崩溃次数，App 启动成功后清零 */
    uint32_t ota_valid;     /* 1=ota 区有完整升级包 */
    uint32_t ota_len;       /* 升级包长度 */
    uint32_t ota_crc;       /* 升级包 CRC32 */
    uint32_t reserved;      /* BOOT_STATE_FLAG_* */
    uint32_t crc;           /* 以上字段的 CRC32 */
} boot_state_t;

typedef struct {
    uint32_t magic;         /* BOOT_HISTORY_MAGIC */
    uint32_t length;        /* 升级进度 */
    uint32_t reserved;
    uint32_t crc;           /* 以上字段的 CRC32 */
} boot_ota_history_t;

typedef char boot_state_size_check[(sizeof(boot_state_t) == BOOT_STATE_SIZE) ? 1 : -1];
typedef char boot_history_size_check[(sizeof(boot_ota_history_t) == BOOT_HISTORY_SLOT) ? 1 : -1];

/* ==========================================================================
 * CRC32 与 Flash
 * ========================================================================== */

uint32_t boot_crc32_update(uint32_t crc, const void *data, uint32_t len);
uint32_t boot_crc32(const void *data, uint32_t len);

int boot_flash_erase(uint32_t addr, uint32_t len);
int boot_flash_write(uint32_t addr, const void *data, uint32_t len);

/* ==========================================================================
 * 启动状态 / 升级历史（Bootloader / App 共用）
 * ========================================================================== */

bool boot_state_valid(const boot_state_t *s);
void boot_state_load(boot_state_t *s);
int boot_state_save(boot_state_t *s);       /* 擦除整个 Meta 区后写入状态（清空历史） */

bool boot_history_last_length(uint32_t *length);
int boot_history_append(const boot_state_t *s, uint32_t length);

/* App 启动确认：tries/crash 清零 */
void boot_confirm(void);

/* 错误日志：追加一行 "[unix] text\\r\\n"，空间不足时整区擦除重写 */
void boot_log_append(uint32_t unix_time, const char *text);

/* 崩溃日志：crash+1 写回状态，异常现场追加到错误日志 */
void boot_crash_log(const uint32_t *frame, uint32_t sp, uint32_t exc_return,
                    uint32_t unix_time);

/* ==========================================================================
 * 配置区（双备份）
 * ========================================================================== */

#define BOOT_CONFIG_MAGIC   0x46434F52U     /* 'ROCF' */

typedef struct {
    uint32_t magic;
    uint32_t len;
    uint32_t crc;
} boot_config_hdr_t;

bool boot_config_valid_at(uint32_t base);
int boot_config_save(const void *data, uint32_t len);
int boot_config_restore(void);

#endif /* ROS_BOOT_META_H */
