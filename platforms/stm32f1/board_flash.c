/*
 * board_flash.c - STM32F103 Flash 原语（擦除按页、编程按半字）
 *
 * 供 KV/错误日志/OTA/App 的 Flash 写入使用，
 * 同时编入 Bootloader 与 App，不调用 libc。
 */

#include "board_flash.h"
#include "partition.h"

#include <stddef.h>

#include "stm32f10x.h"

#define BOARD_FLASH_PAGE         1024U           /* F103 中容量：1KB/页 */

/* Flash 寄存器位（避免与设备头宏重名） */
#define BOARD_FLASH_SR_BSY       (1UL << 0)
#define BOARD_FLASH_SR_PGERR     (1UL << 2)
#define BOARD_FLASH_SR_WRPRTERR  (1UL << 4)
#define BOARD_FLASH_CR_PG        (1UL << 0)
#define BOARD_FLASH_CR_PER       (1UL << 1)
#define BOARD_FLASH_CR_STRT      (1UL << 6)
#define BOARD_FLASH_CR_LOCK      (1UL << 7)

#define BOARD_FLASH_TIMEOUT      0x200000UL  /* Flash 忙等上限（约几十 ms） */

/* 等待 Flash 操作完成并检查错误标志；返回 0 成功 */
static int board_flash_wait(void)
{
    uint32_t timeout = BOARD_FLASH_TIMEOUT;

    while ((FLASH->SR & BOARD_FLASH_SR_BSY) != 0U) {
        if (--timeout == 0U) {
            return -1;
        }
    }
    if ((FLASH->SR & (BOARD_FLASH_SR_PGERR | BOARD_FLASH_SR_WRPRTERR)) != 0U) {
        FLASH->SR = BOARD_FLASH_SR_PGERR | BOARD_FLASH_SR_WRPRTERR;  /* 写 1 清除 */
        return -1;
    }
    return 0;
}

/* 解锁 Flash（重复调用安全） */
static void board_flash_unlock(void)
{
    if ((FLASH->CR & BOARD_FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR = 0x45670123U;
        FLASH->KEYR = 0xCDEF89ABU;
    }
}

/* 上锁 Flash */
static void board_flash_lock(void)
{
    FLASH->CR |= BOARD_FLASH_CR_LOCK;
}

/*
 * 擦除 addr 开始、覆盖 len 字节的 Flash 区域（按页向上取整）。
 * 约束：addr 必须在 A 区之后且按页对齐；不允许擦除 Bootloader 自身。
 */
int board_flash_erase(uint32_t addr, uint32_t len)
{
    uint32_t page;
    uint32_t count;
    int rc = 0;

    if (len == 0U || addr < FLASH_KV_BASE0 ||
        (addr % BOARD_FLASH_PAGE) != 0U) {
        return -1;
    }
    count = (len + BOARD_FLASH_PAGE - 1U) / BOARD_FLASH_PAGE;

    board_flash_unlock();
    for (page = 0U; page < count; page++) {
        uint32_t p = addr + page * BOARD_FLASH_PAGE;

        if (board_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = BOARD_FLASH_CR_PER;      /* 页擦除 */
        FLASH->AR = p;                      /* 目标地址 */
        FLASH->CR |= BOARD_FLASH_CR_STRT;    /* 启动擦除 */
        if (board_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = 0U;
    }
    board_flash_lock();
    return rc;
}

/*
 * 从 addr 开始写入 len 字节（半字编程）。
 * 奇数长度时最后一个半字的高字节补 0xFF（Flash 擦除态），不参与 CRC。
 * 约束：addr 必须 2 字节对齐且在 A 区之后。
 */
int board_flash_write(uint32_t addr, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t i;
    int rc = 0;

    if (data == NULL || len == 0U || addr < FLASH_KV_BASE0 ||
        (addr & 1U) != 0U) {
        return -1;
    }

    board_flash_unlock();
    for (i = 0U; i < len; i += 2U) {
        uint16_t hw = (uint16_t)p[i];

        if ((i + 1U) < len) {
            hw |= (uint16_t)((uint16_t)p[i + 1U] << 8);
        } else {
            hw |= 0xFF00U;
        }
        if (board_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = BOARD_FLASH_CR_PG;       /* 半字编程 */
        *(volatile uint16_t *)(uintptr_t)(addr + i) = hw;
        if (board_flash_wait() != 0) {
            rc = -1;
            break;
        }
        FLASH->CR = 0U;
    }
    board_flash_lock();
    return rc;
}
