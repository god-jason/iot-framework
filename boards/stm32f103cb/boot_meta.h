/*
 * boot_meta.h - STM32F103CBT6 启动状态、升级历史、Flash 与配置区
 *
 * Boot Meta 分区（4KB）布局：
 *   [0x000] boot_state_t        32B  启动状态（tries / crash / ota 标识）
 *   [0x020] boot_ota_history_t  16B × N  升级进度历史（追加写）
 *
 * 启动流程：
 *   Bootloader 每次启动 tries+1 后启动 App；App 初始化完成调用 boot_confirm()
 *   把 tries/crash 清零。tries 超过 5 次时停留在 Bootloader 等待串口 OTA。
 *
 * OTA 升级：
 *   ota 区保存原始编译结果 bin（无包头），长度和 CRC 存在 boot_state 里；
 *   Bootloader 按 1KB 分块把 ota 区拷入 App 区，每拷完一块追加一条历史
 *   记录（length=已写字节数）；掉电后从未确认的偏移继续。全部完成并校验
 *   CRC 通过后，擦除整个 Meta 区并重写 boot_state（ota_valid=0）。
 */

#ifndef ROS_BOOT_META_H
#define ROS_BOOT_META_H

#include "stm32f10x.h"
#include "partition.h"

#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 * 启动状态与升级历史
 * ========================================================================== */

#define BOOT_FLASH_PAGE         1024U           /* F103 中容量：1KB/页 */

#define BOOT_STATE_MAGIC        0x54415453U     /* 'STAT' */
#define BOOT_HISTORY_MAGIC      0x54534948U     /* 'HIST' */

#define BOOT_STATE_SIZE         32U
#define BOOT_HISTORY_SLOT       16U
#define BOOT_HISTORY_BASE       (BOOT_META_BASE + BOOT_STATE_SIZE)
#define BOOT_HISTORY_COUNT      ((BOOT_META_SIZE - BOOT_STATE_SIZE) / BOOT_HISTORY_SLOT)

#define BOOT_MAX_TRIES          5U              /* 连续启动失败上限 */
#define BOOT_MAX_CRASH          5U              /* 连续崩溃上限（F103 仅记录） */

/* boot_state_t.reserved 位定义（F407 使用，F103 保留为 0） */
#define BOOT_STATE_FLAG_BACKUP  0x00000001U     /* 备份区有效 */

/* 启动状态：固定位于 Meta 区起始 */
typedef struct {
    uint32_t magic;         /* BOOT_STATE_MAGIC */
    uint32_t tries;         /* 启动次数，App 启动成功后清零 */
    uint32_t crash;         /* 崩溃次数，App 启动成功后清零 */
    uint32_t ota_valid;     /* 1=ota 区有完整升级包，需要校验并拷贝到 App */
    uint32_t ota_len;       /* 升级包长度 */
    uint32_t ota_crc;       /* 升级包 CRC32 */
    uint32_t reserved;      /* BOOT_STATE_FLAG_* */
    uint32_t crc;           /* 以上字段的 CRC32 */
} boot_state_t;

/* 升级进度历史：追加写入，length 为已写入 App 的字节数 */
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
void boot_state_load(boot_state_t *s);      /* 无有效状态时返回默认值 */
int boot_state_save(boot_state_t *s);       /* 擦除整个 Meta 区后写入状态（清空历史） */

bool boot_history_last_length(uint32_t *length);        /* 无历史返回 false */
int boot_history_append(const boot_state_t *s, uint32_t length);

/* App 启动确认：tries/crash 清零 */
void boot_confirm(void);

/* ==========================================================================
 * 错误日志与崩溃记录
 * ========================================================================== */

/* 追加一行 "[unix] text\r\n"；空间不足时整区擦除后从头部重写 */
void boot_log_append(uint32_t unix_time, const char *text);

/* HardFault：crash+1 并写回状态，异常现场追加到错误日志 */
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
