#include "iot_partition.h"
#include "iot_memory.h"

#define IOT_PARTITION_MAX        8
#define IOT_PARTITION_FLASH_SIZE 0x10000   /* 64KB 模拟 Flash */

static iot_partition_t s_parts[IOT_PARTITION_MAX];
static int s_part_count = 0;
static uint8_t s_flash[IOT_PARTITION_FLASH_SIZE];

static const iot_partition_t *partition_find(iot_partition_type_t type)
{
    int i;
    for (i = 0; i < s_part_count; i++) {
        if (s_parts[i].type == type) {
            return &s_parts[i];
        }
    }
    return NULL;
}

int iot_partition_register(const iot_partition_t *partition)
{
    if (partition == NULL || partition->size == 0) {
        return IOT_ERR_PARAM;
    }
    if (s_part_count >= IOT_PARTITION_MAX ||
        partition->address + partition->size > IOT_PARTITION_FLASH_SIZE) {
        return IOT_ERR_OVERFLOW;
    }
    s_parts[s_part_count++] = *partition;
    return IOT_OK;
}

const iot_partition_t *iot_partition_get(iot_partition_type_t type)
{
    return partition_find(type);
}

int iot_partition_read(iot_partition_type_t type, uint32_t offset,
                       void *buffer, uint32_t len)
{
    const iot_partition_t *p = partition_find(type);
    if (p == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    if (buffer == NULL || offset + len > p->size ||
        p->address + offset + len > IOT_PARTITION_FLASH_SIZE) {
        return IOT_ERR_INVALID;
    }
    iot_memory_copy(buffer, &s_flash[p->address + offset], len);
    return IOT_OK;
}

int iot_partition_write(iot_partition_type_t type, uint32_t offset,
                        const void *buffer, uint32_t len)
{
    const iot_partition_t *p = partition_find(type);
    if (p == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    if (!p->writable) {
        return IOT_ERR_STATE;
    }
    if (buffer == NULL || offset + len > p->size ||
        p->address + offset + len > IOT_PARTITION_FLASH_SIZE) {
        return IOT_ERR_INVALID;
    }
    iot_memory_copy(&s_flash[p->address + offset], buffer, len);
    return IOT_OK;
}

int iot_partition_erase(iot_partition_type_t type, uint32_t offset, uint32_t len)
{
    const iot_partition_t *p = partition_find(type);
    if (p == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    if (offset + len > p->size ||
        p->address + offset + len > IOT_PARTITION_FLASH_SIZE) {
        return IOT_ERR_INVALID;
    }
    iot_memory_set(&s_flash[p->address + offset], 0xFF, len);
    return IOT_OK;
}