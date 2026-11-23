/*
 * boot_config.h - STM32 配置区双备份（F1/F4 共用）
 *
 * 配置区主区/备份区地址与大小见 platforms/stm32<family> 或 apps/<board> 中的 partition.h。
 * 每条记录 = 头部（magic/长度/CRC）+ 负载；写入时先备份后主区。
 */

#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include "partition.h"

#include <stdbool.h>
#include <stdint.h>

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

#endif /* BOOT_CONFIG_H */
