/*
 * boot_meta.h - STM32F407VE A/B/C OTA 启动元数据、Flash 操作与启动确认
 *
 * Flash 布局（512KB，F407 扇区：4x16K + 1x64K + 3x128K，详见 partition.h）：
 *   1 Bootloader  0x08000000 16K（S0）
 *   2 Boot Meta   0x08004000 16K（S1，追加记录，写满擦除）
 *   3 配置主区    0x08008000 16K（S2）
 *   4 配置备份    0x0800C000 16K（S3）
 *   5 系统日志    0x08010000 64K（S4，文本追加，带 Unix 时间）
 *   6 App 主程序  0x08020000 128K（S5）
 *   7 App 备份    0x08040000 128K（S6）
 *   8 App 升级包  0x08060000 128K（S7）
 */

#ifndef ROS_BOOT_META_H
#define ROS_BOOT_META_H

#include "stm32f4xx.h"
#include "partition.h"

#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 * 分区与元数据定义
 * ========================================================================== */

#define BOOT_META_MAGIC     0x4D534F52U     /* 'ROSM' */
#define BOOT_OTA_MAGIC      0x55534F52U     /* 'ROSU' */

#define BOOT_STATE_IDLE     0U
#define BOOT_STATE_TRYING   1U

#define BOOT_MAX_TRIES      3U              /* 新固件试运行启动次数 */
#define BOOT_MAX_CRASH      3U              /* 试运行期崩溃次数上限 */

/* 启动元数据：Meta 日志区内 64B 追加记录，seq 最大且 CRC 有效的记录优先 */
typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint32_t state;                         /* BOOT_STATE_* */
    uint32_t tries;                         /* 剩余试运行启动次数 */
    uint32_t crash;                         /* 试运行期崩溃次数 */
    uint32_t b_valid;                       /* B 区备份有效标志 */
    uint32_t reserved[9];
    uint32_t meta_crc;
} boot_meta_t;

/* C 区升级包头部：位于 C 区起始，payload 紧随其后；头部最后写入 */
typedef struct {
    uint32_t magic;                         /* BOOT_OTA_MAGIC */
    uint32_t len;                           /* payload 长度 */
    uint32_t crc;                           /* payload 的 CRC32 */
    uint32_t hdr_crc;                       /* 前三项的 CRC32 */
} boot_ota_hdr_t;

/* 元数据记录必须正好占满一个日志槽 */
typedef char boot_meta_size_check[(sizeof(boot_meta_t) == BOOT_META_SLOT) ? 1 : -1];

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

void boot_confirm(void);

/* 系统日志：追加一行 "[unix] text\\r\\n"，空间不足时整区擦除重写 */
void boot_log_append(uint32_t unix_time, const char *text);

/* 崩溃日志：写系统日志并累加崩溃计数 */
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
