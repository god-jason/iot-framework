/*
 * boot_meta.h - STM32F103CBT6 启动元数据、升级日志、Flash 与配置区
 *
 * Boot Meta 分区（4KB）同时保存：
 *   - 数据 meta：升级包长度/CRC、状态、已写入 App 的偏移；
 *   - 升级日志：升级过程中追加的 32B 进度记录。
 * 掉电后按最后一条有效记录继续，保证 App 区最终是完整镜像。
 *
 * 本文件同时编入 Bootloader 与 App：
 *   - Bootloader 使用 Flash 驱动、配置区、Boot Meta 与错误日志；
 *   - App 使用 boot_crash_log() 写错误日志。
 */

#ifndef ROS_BOOT_META_H
#define ROS_BOOT_META_H

#include "stm32f10x.h"
#include "partition.h"

#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 * Flash 与 Boot Meta（数据 meta + 追加升级日志）
 * ========================================================================== */

#define BOOT_FLASH_PAGE         1024U           /* F103 中容量：1KB/页 */

#define BOOT_META_MAGIC         0x544F4F52U     /* 'ROOT' */
#define BOOT_META_SLOT          32U             /* 单条记录固定 32 字节 */
#define BOOT_META_RECORDS       (BOOT_META_SIZE / BOOT_META_SLOT)

#define BOOT_META_STATE_READY    1U             /* 升级包接收完整，待写入 App */
#define BOOT_META_STATE_APPLYING 2U             /* 正在写入 App，offset 为已写字节数 */

/*
 * 元数据/进度记录：追加写入 Boot Meta 区，seq 最大的有效记录为当前状态。
 * READY 记录同时充当“升级有效标识”。
 */
typedef struct {
    uint32_t magic;         /* BOOT_META_MAGIC */
    uint32_t seq;           /* 每次保存自增 */
    uint32_t state;         /* BOOT_META_STATE_* */
    uint32_t offset;        /* 已写入 App 区的字节数 */
    uint32_t len;           /* 升级包总长度 */
    uint32_t crc;           /* 升级包 CRC32 */
    uint32_t reserved;
    uint32_t rec_crc;       /* 前面所有字段的 CRC32 */
} boot_meta_rec_t;

typedef char boot_meta_rec_size_check[(sizeof(boot_meta_rec_t) == BOOT_META_SLOT) ? 1 : -1];

/* ==========================================================================
 * CRC32 与 Flash
 * ========================================================================== */

uint32_t boot_crc32_update(uint32_t crc, const void *data, uint32_t len);
uint32_t boot_crc32(const void *data, uint32_t len);

int boot_flash_erase(uint32_t addr, uint32_t len);
int boot_flash_write(uint32_t addr, const void *data, uint32_t len);

/* ==========================================================================
 * Boot Meta 记录（Bootloader / App 共用）
 * ========================================================================== */

bool boot_meta_rec_valid(const boot_meta_rec_t *r);
bool boot_meta_rec_load(boot_meta_rec_t *r);    /* 无有效记录时返回 false */
int boot_meta_rec_save(boot_meta_rec_t *r);     /* 追加一条记录，写满后整区擦除 */

/* ==========================================================================
 * 错误日志与崩溃记录
 * ========================================================================== */

/* 追加一行 "[unix] text\r\n"；空间不足时整区擦除后从头部重写 */
void boot_log_append(uint32_t unix_time, const char *text);

/* HardFault：把异常现场追加到错误日志（带 Unix 时间），随后由调用者复位 */
void boot_crash_log(const uint32_t *frame, uint32_t sp, uint32_t exc_return,
                    uint32_t unix_time);

/* ==========================================================================
 * 配置区（双备份）
 * ========================================================================== */

#define BOOT_CONFIG_MAGIC   0x46434F52U     /* 'ROCF' */

typedef struct {
    uint32_t magic;
    uint32_t len;       /* 负载长度 */
    uint32_t crc;       /* 负载 CRC32 */
} boot_config_hdr_t;

/* 指定配置区是否有效（magic/长度/CRC 均校验） */
bool boot_config_valid_at(uint32_t base);

/* 写入配置：先写备份区，再写主区；成功返回 0 */
int boot_config_save(const void *data, uint32_t len);

/*
 * 启动时校验主配置；无效且备份有效则把备份还原到主区。
 * 返回：1=已还原，0=主区有效无需处理，-2=主备均无效，-1=还原失败。
 */
int boot_config_restore(void);

#endif /* ROS_BOOT_META_H */
