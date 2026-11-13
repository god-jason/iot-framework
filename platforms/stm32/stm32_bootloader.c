/*
 * stm32_bootloader.c - STM32 平台侧的 Bootloader 移植层
 *
 * 只做三件平台相关的事：
 *   1) 用 hal_flash 注册 Flash 读写擦除与默认分区布局；
 *   2) 提供 SRAM 范围与向量表跳转（切 MSP + 跳复位地址）；
 *   3) 把串口升级端口绑定到 hal_uart（weak 引用，未启用 HAL 也可链接）。
 * 分区/镜像/升级/协议等公共逻辑全部在 bootloader/bootloader.c。
 */

#if defined(ROS_PLATFORM_STM32)

#include "stm32_bootloader.h"
#include "hal_flash.h"
#include "ros.h"
#include "common.h"

#include <string.h>

#define BOOT_TAG "boot"

static hal_flash_t      s_flash;
static boot_flash_ops_t s_ops;

/* ------------------------------ Flash 移植 ------------------------------ */

static int stm32_flash_read(uint32_t address, void *buffer, uint32_t length)
{
    return (hal_flash_read(&s_flash, (uintptr_t)address, buffer,
                           (size_t)length) == HAL_OK) ? 0 : -1;
}

static int stm32_flash_write(uint32_t address, const void *buffer,
                             uint32_t length)
{
    return (hal_flash_write(&s_flash, (uintptr_t)address, buffer,
                            (size_t)length) == HAL_OK) ? 0 : -1;
}

static int stm32_flash_erase(uint32_t address, uint32_t length)
{
    return (hal_flash_erase(&s_flash, (uintptr_t)address,
                            (size_t)length) == HAL_OK) ? 0 : -1;
}

/* 登记一个默认分区（失败不影响其它分区） */
static void stm32_part_add(partition_type_t type, uint32_t offset,
                           uint32_t size, bool writable)
{
    partition_t part;

    memset(&part, 0, sizeof(part));
    part.type = type;
    part.address = (uint32_t)s_flash.info.start + offset;
    part.size = size;
    part.erase_size = (uint32_t)s_flash.info.erase_size;
    part.writable = writable;

    if (partition_register(&part) != 0) {
        LOG_E(BOOT_TAG, "register partition failed: type=%d", (int)type);
    }
}

void boot_port_init(void)
{
    memset(&s_flash, 0, sizeof(s_flash));
    s_flash.base.state = HAL_STATE_RESET;
    if (hal_flash_init(&s_flash) != HAL_OK) {
        LOG_E(BOOT_TAG, "flash init failed");
        return;
    }

    memset(&s_ops, 0, sizeof(s_ops));
    s_ops.start = (uint32_t)s_flash.info.start;
    s_ops.size = (uint32_t)s_flash.info.size;
    s_ops.erase_size = (uint32_t)s_flash.info.erase_size;
    s_ops.read = stm32_flash_read;
    s_ops.write = stm32_flash_write;
    s_ops.erase = stm32_flash_erase;
    if (boot_register_flash(&s_ops) != 0) {
        return;
    }

    stm32_part_add(PART_BOOT, STM32_BOOT_OFF_BOOT,
                   STM32_BOOT_SIZE_BOOT, false);
    stm32_part_add(PART_APP, STM32_BOOT_OFF_APP,
                   STM32_BOOT_SIZE_APP, true);
    stm32_part_add(PART_OTA, STM32_BOOT_OFF_OTA,
                   STM32_BOOT_SIZE_OTA, true);
    stm32_part_add(PART_CONFIG, STM32_BOOT_OFF_CONFIG,
                   STM32_BOOT_SIZE_CONFIG, true);
}

/* ------------------------------ 跳转 ------------------------------ */

uint32_t boot_port_ram_start(void)
{
    return (uint32_t)STM32_BOOT_RAM_START;
}

uint32_t boot_port_ram_end(void)
{
    return (uint32_t)STM32_BOOT_RAM_END;
}

int boot_port_jump(uint32_t entry)
{
    uint32_t sp = *(volatile uint32_t *)entry;
    uint32_t pc = *(volatile uint32_t *)(entry + 4U);

    /* 切栈到 APP 的初始栈顶，再跳其复位向量（Thumb 地址） */
    __asm volatile ("cpsid i" ::: "memory");
    __asm volatile ("msr msp, %0" :: "r" (sp));
    __asm volatile ("msr control, %0" :: "r" (0));
    __asm volatile ("isb");
    ((void (*)(void))(uintptr_t)pc)();

    return -1;      /* 正常情况下不会返回 */
}

/* ------------------------------ 串口绑定 ------------------------------ */

/*
 * Cube HAL 便捷端口：weak 引用，未链接 HAL 时不会产生未定义符号，
 * 此时 boot_uart_wait_update() 返回 -2（未配置端口）。
 */
extern int hal_uart_receive(void *handle, uint8_t *data, size_t length,
                            uint32_t timeout) __attribute__((weak));
extern int hal_uart_transmit(void *handle, const uint8_t *data, size_t length,
                             uint32_t timeout) __attribute__((weak));

static int boot_hal_read(uint8_t *buf, uint32_t len, uint32_t timeout_ms,
                         void *user)
{
    if (hal_uart_receive == NULL) {
        return 0;
    }
    if (hal_uart_receive(user, buf, (size_t)len, timeout_ms) != HAL_OK) {
        return 0;               /* 超时/错误按“无数据”处理，交给上层判超时 */
    }
    return (int)len;
}

static int boot_hal_write(const uint8_t *buf, uint32_t len,
                          uint32_t timeout_ms, void *user)
{
    if (hal_uart_transmit == NULL) {
        return -1;
    }
    return (hal_uart_transmit(user, buf, (size_t)len, timeout_ms) == HAL_OK)
           ? (int)len : -1;
}

void boot_set_uart_handle(void *uart, uint32_t timeout_ms)
{
    boot_set_uart_timeout(timeout_ms);
    boot_set_uart(boot_hal_read, boot_hal_write, uart);
}

#endif /* ROS_PLATFORM_STM32 */
