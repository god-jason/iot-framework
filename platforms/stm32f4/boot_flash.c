/*
 * boot_flash.c - STM32F407VE Flash 原语（扇区擦除、字节编程）
 *
 * 供 stm32_meta.c 的 Meta/配置/日志/OTA 写入使用，
 * 同时编入 Bootloader 与 App，不调用 libc。
 */

#include "boot_flash.h"
#include "partition.h"

#include <stddef.h>

#include "stm32f4xx.h"

#define BOOT_FLASH_CR_PG        (1UL << 0)
#define BOOT_FLASH_CR_SER       (1UL << 1)
#define BOOT_FLASH_CR_SNB       (0xFUL << 3)
#define BOOT_FLASH_CR_PSIZE_8   (0UL << 8)
#define BOOT_FLASH_CR_STRT      (1UL << 16)
#define BOOT_FLASH_CR_LOCK      (1UL << 31)

#define BOOT_FLASH_SR_BSY       (1UL << 16)
#define BOOT_FLASH_SR_ERRORS    0x000002F2UL   /* OPERR/WRPERR/PGAERR/PGPERR/PGSERR/RDERR */
#define BOOT_FLASH_TIMEOUT      0x400000UL

/* STM32F407VE 512KB 扇区表 */
static const uint32_t s_sector_base[] = {
    0x08000000U, 0x08004000U, 0x08008000U, 0x0800C000U,
    0x08010000U, 0x08020000U, 0x08040000U, 0x08060000U
};

static const uint32_t s_sector_size[] = {
    16U * 1024U, 16U * 1024U, 16U * 1024U, 16U * 1024U,
    64U * 1024U, 128U * 1024U, 128U * 1024U, 128U * 1024U
};

static int boot_flash_wait(void)
{
    uint32_t timeout = BOOT_FLASH_TIMEOUT;

    while ((FLASH->SR & BOOT_FLASH_SR_BSY) != 0U) {
        if (--timeout == 0U) {
            return -1;
        }
    }
    if ((FLASH->SR & BOOT_FLASH_SR_ERRORS) != 0U) {
        FLASH->SR = BOOT_FLASH_SR_ERRORS;    /* 写 1 清除 */
        return -1;
    }
    return 0;
}

static void boot_flash_unlock(void)
{
    if ((FLASH->CR & BOOT_FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR = 0x45670123U;
        FLASH->KEYR = 0xCDEF89ABU;
    }
}

static void boot_flash_lock(void)
{
    FLASH->CR |= BOOT_FLASH_CR_LOCK;
}

static int boot_sector_index(uint32_t addr)
{
    uint32_t i;

    for (i = 0U; i < sizeof(s_sector_base) / sizeof(s_sector_base[0]); i++) {
        if (addr >= s_sector_base[i] &&
            addr < s_sector_base[i] + s_sector_size[i]) {
            return (int)i;
        }
    }
    return -1;
}

/*
 * 擦除覆盖 [addr, addr+len) 的 Flash 扇区。
 * addr 必须位于扇区起始；S0（Bootloader）不可擦除。
 */
int boot_flash_erase(uint32_t addr, uint32_t len)
{
    uint32_t end;
    uint32_t cur;
    int rc = 0;

    if (len == 0U || addr < BOOT_META_BASE) {
        return -1;
    }

    end = addr + len;
    cur = addr;
    boot_flash_unlock();
    while (cur < end) {
        int idx = boot_sector_index(cur);
        uint32_t base;

        if (idx < 0 || cur != s_sector_base[idx]) {
            rc = -1;
            break;
        }
        base = s_sector_base[idx];
        if (boot_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = BOOT_FLASH_CR_PSIZE_8 | BOOT_FLASH_CR_SER |
                    ((uint32_t)idx << 3);
        FLASH->CR |= BOOT_FLASH_CR_STRT;
        if (boot_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = 0U;
        cur = base + s_sector_size[idx];
    }
    boot_flash_lock();
    return rc;
}

/* 从 addr 开始写入 len 字节（字节编程，调用前已擦除） */
int boot_flash_write(uint32_t addr, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;
    int rc = 0;

    if (data == NULL || len == 0U || addr < BOOT_META_BASE) {
        return -1;
    }

    boot_flash_unlock();
    for (i = 0U; i < len; i++) {
        if (boot_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = BOOT_FLASH_CR_PSIZE_8 | BOOT_FLASH_CR_PG;
        *(volatile uint8_t *)(uintptr_t)(addr + i) = p[i];
        if (boot_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = 0U;
    }
    boot_flash_lock();
    return rc;
}
