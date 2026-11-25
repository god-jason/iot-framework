/*
 * boot_kv.c - KV 存储接入（kvfs + 内部 Flash 原语）
 *
 * 读：内存映射直读（volatile 逐字节，避免依赖 libc）；
 * 写/擦：board_flash_write / board_flash_erase（platforms/stm32<family>）。
 * 不依赖 libc / RTOS，可编入 -nostdlib 的 Bootloader。
 */

#include "boot_kv.h"
#include "board_flash.h"

#include <stddef.h>

/* 工作缓冲：不小于单条 value 上限 */
static uint8_t s_work[BOOT_KV_MAX_VALUE];
static kvfs_t s_kv;
static uint8_t s_ready;

static int kv_flash_read(void *ctx, uint32_t addr, void *buf, uint32_t size)
{
    volatile const uint8_t *src = (volatile const uint8_t *)(uintptr_t)addr;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t i;

    (void)ctx;
    if (buf == NULL && size != 0U) {
        return -1;
    }
    for (i = 0U; i < size; i++) {
        dst[i] = src[i];
    }
    return 0;
}

static int kv_flash_program(void *ctx, uint32_t addr, const void *buf,
                            uint32_t size)
{
    (void)ctx;
    return board_flash_write(addr, buf, size);
}

static int kv_flash_erase(void *ctx, uint32_t addr, uint32_t size)
{
    (void)ctx;
    return board_flash_erase(addr, size);
}

static const kvfs_blockdev_t s_dev = {
    .read = kv_flash_read,
    .program = kv_flash_program,
    .erase = kv_flash_erase,
    .sync = NULL,
    .ctx = NULL,
    .start_addr = FLASH_KV_BASE0,
    .total_size = FLASH_KV_SIZE * 2U,
    .erase_size = FLASH_KV_SIZE,
    .program_size = 4U
};

static const kvfs_config_t s_cfg = {
    .workbuf = s_work,
    .workbuf_size = sizeof(s_work),
    .max_value = BOOT_KV_MAX_VALUE,
    .readonly = 0U,
    .lock = NULL,
    .unlock = NULL,
    .mutex_ctx = NULL
};

kvfs_t *boot_kv(void)
{
    return &s_kv;
}

int boot_kv_init(void)
{
    int rc;

    if (s_ready != 0U) {
        return 0;
    }
    rc = kvfs_init(&s_kv, &s_dev, &s_cfg);
    if (rc != KVFS_OK) {
        return -1;
    }

    rc = kvfs_mount(&s_kv);
    if (rc == KVFS_ERR_FORMAT || rc == KVFS_ERR_VERSION) {
        /* 首次使用或格式不兼容：自动格式化 */
        if (kvfs_format(&s_kv) != KVFS_OK) {
            return -1;
        }
        rc = KVFS_OK;
    }
    if (rc != KVFS_OK) {
        return -1;
    }

    s_ready = 1U;
    return 0;
}

int boot_kv_put(uint32_t key, const void *data, uint32_t len)
{
    if (boot_kv_init() != 0) {
        return -1;
    }
    return (kvfs_put(&s_kv, key, data, len) == KVFS_OK) ? 0 : -1;
}

int boot_kv_get(uint32_t key, void *buf, uint32_t cap, uint32_t *len)
{
    uint32_t n = 0U;
    int rc;

    if (boot_kv_init() != 0) {
        return -1;
    }
    rc = kvfs_get(&s_kv, key, buf, cap, &n);
    if (rc == KVFS_OK) {
        if (len != NULL) {
            *len = n;
        }
        return 0;
    }
    if (rc == KVFS_ERR_NOENT) {
        return -2;
    }
    if (len != NULL) {
        *len = n;
    }
    return (rc == KVFS_ERR_NOMEM) ? -3 : -1;
}

int boot_kv_del(uint32_t key)
{
    int rc;

    if (boot_kv_init() != 0) {
        return -1;
    }
    rc = kvfs_del(&s_kv, key);
    return (rc == KVFS_OK || rc == KVFS_ERR_NOENT) ? 0 : -1;
}

int boot_kv_exists(uint32_t key)
{
    int rc;

    if (boot_kv_init() != 0) {
        return -1;
    }
    rc = kvfs_exists(&s_kv, key);
    return (rc < 0) ? -1 : rc;
}
