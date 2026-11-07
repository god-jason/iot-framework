#include "partition.h"
#include "iot_memory.h"

#define PARTITION_MAX        8
#define PARTITION_FLASH_SIZE 0x10000   /* 64KB 模拟 Flash */

static partition_t s_parts[PARTITION_MAX];
static int s_part_count = 0;
static uint8_t s_flash[PARTITION_FLASH_SIZE];

static const partition_t *partition_find(partition_type_t type)
{
    int i;
    for (i = 0; i < s_part_count; i++) {
        if (s_parts[i].type == type) {
            return &s_parts[i];
        }
    }
    return NULL;
}

int partition_register(const partition_t *partition)
{
    if (partition == NULL || partition->size == 0) {
        return -1;
    }
    if (s_part_count >= PARTITION_MAX ||
        partition->address + partition->size > PARTITION_FLASH_SIZE) {
        return -1;
    }
    s_parts[s_part_count++] = *partition;
    return IOT_OK;
}

const partition_t *partition_get(partition_type_t type)
{
    return partition_find(type);
}

int partition_read(partition_type_t type, uint32_t offset,
                       void *buffer, uint32_t len)
{
    const partition_t *p = partition_find(type);
    if (p == NULL) {
        return -1;
    }
    if (buffer == NULL || offset + len > p->size ||
        p->address + offset + len > PARTITION_FLASH_SIZE) {
        return -1;
    }
    iot_memory_copy(buffer, &s_flash[p->address + offset], len);
    return IOT_OK;
}

int partition_write(partition_type_t type, uint32_t offset,
                        const void *buffer, uint32_t len)
{
    const partition_t *p = partition_find(type);
    if (p == NULL) {
        return -1;
    }
    if (!p->writable) {
        return -1;
    }
    if (buffer == NULL || offset + len > p->size ||
        p->address + offset + len > PARTITION_FLASH_SIZE) {
        return -1;
    }
    iot_memory_copy(&s_flash[p->address + offset], buffer, len);
    return IOT_OK;
}

int partition_erase(partition_type_t type, uint32_t offset, uint32_t len)
{
    const partition_t *p = partition_find(type);
    if (p == NULL) {
        return -1;
    }
    if (offset + len > p->size ||
        p->address + offset + len > PARTITION_FLASH_SIZE) {
        return -1;
    }
    iot_memory_set(&s_flash[p->address + offset], 0xFF, len);
    return IOT_OK;
}