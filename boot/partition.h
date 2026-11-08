#ifndef PARTITION_H
#define PARTITION_H

/*
 * 固件分区表：描述 Boot / APP / OTA / 配置等分区的地址、大小与可写性。
 * host 环境下用内置 RAM 模拟 Flash；真实平台应委托给 hal_flash_t。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PART_BOOT,
    PART_APP,
    PART_APP_BACKUP,
    PART_OTA,
    PART_CONFIG,
    PART_LOG
} partition_type_t;

typedef struct {
    partition_type_t type;
    uint32_t address;
    uint32_t size;
    uint32_t erase_size;
    bool writable;
} partition_t;

int partition_register(const partition_t *partition);
const partition_t *partition_get(partition_type_t type);

int partition_read(partition_type_t type, uint32_t offset,
                       void *buffer, uint32_t len);
int partition_write(partition_type_t type, uint32_t offset,
                        const void *buffer, uint32_t len);
int partition_erase(partition_type_t type, uint32_t offset, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* PARTITION_H */