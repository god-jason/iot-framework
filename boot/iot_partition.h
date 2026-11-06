#ifndef IOT_PARTITION_H
#define IOT_PARTITION_H

/*
 * 固件分区表：描述 Boot / APP / OTA / 配置等分区的地址、大小与可写性。
 * host 环境下用内置 RAM 模拟 Flash；真实平台应委托给 iot_port_flash_t。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_PART_BOOT,
    IOT_PART_APP,
    IOT_PART_APP_BACKUP,
    IOT_PART_OTA,
    IOT_PART_CONFIG,
    IOT_PART_LOG
} iot_partition_type_t;

typedef struct {
    iot_partition_type_t type;
    uint32_t address;
    uint32_t size;
    uint32_t erase_size;
    bool writable;
} iot_partition_t;

int iot_partition_register(const iot_partition_t *partition);
const iot_partition_t *iot_partition_get(iot_partition_type_t type);

int iot_partition_read(iot_partition_type_t type, uint32_t offset,
                       void *buffer, uint32_t len);
int iot_partition_write(iot_partition_type_t type, uint32_t offset,
                        const void *buffer, uint32_t len);
int iot_partition_erase(iot_partition_type_t type, uint32_t offset, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* IOT_PARTITION_H */