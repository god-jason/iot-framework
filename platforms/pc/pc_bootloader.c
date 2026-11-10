/*
 * pc_bootloader.c - 主机模拟端的 Bootloader 移植层
 *
 * 复用 platforms/pc 的 RAM 模拟 Flash，让 iot-test 能完整跑通
 * 分区、升级、校验与安装流程；跳转在主机上没有意义，返回 -1，
 * 测试可通过 boot_set_jump() 注入假跳转。
 */

#if defined(IOT_PLATFORM_PC)

#include "bootloader.h"
#include "hal_flash.h"
#include "log.h"

#include <string.h>

#define BOOT_TAG "boot"

/* 与 STM32 默认布局保持一致，便于同一套测试与上位机工具复用 */
#define PC_BOOT_OFF_BOOT    0x00000U
#define PC_BOOT_SIZE_BOOT   0x04000U
#define PC_BOOT_OFF_APP     0x04000U
#define PC_BOOT_SIZE_APP    0x07000U
#define PC_BOOT_OFF_OTA     0x0B000U
#define PC_BOOT_SIZE_OTA    0x04000U
#define PC_BOOT_OFF_CONFIG  0x0F000U
#define PC_BOOT_SIZE_CONFIG 0x01000U

static hal_flash_t      s_flash;
static boot_flash_ops_t s_ops;

static int pc_flash_read(uint32_t address, void *buffer, uint32_t length)
{
    return (hal_flash_read(&s_flash, (uintptr_t)address, buffer,
                           (size_t)length) == HAL_OK) ? 0 : -1;
}

static int pc_flash_write(uint32_t address, const void *buffer, uint32_t length)
{
    return (hal_flash_write(&s_flash, (uintptr_t)address, buffer,
                            (size_t)length) == HAL_OK) ? 0 : -1;
}

static int pc_flash_erase(uint32_t address, uint32_t length)
{
    return (hal_flash_erase(&s_flash, (uintptr_t)address,
                            (size_t)length) == HAL_OK) ? 0 : -1;
}

static void pc_part_add(partition_type_t type, uint32_t offset,
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
    s_ops.read = pc_flash_read;
    s_ops.write = pc_flash_write;
    s_ops.erase = pc_flash_erase;
    if (boot_register_flash(&s_ops) != 0) {
        return;
    }

    pc_part_add(PART_BOOT, PC_BOOT_OFF_BOOT, PC_BOOT_SIZE_BOOT, false);
    pc_part_add(PART_APP, PC_BOOT_OFF_APP, PC_BOOT_SIZE_APP, true);
    pc_part_add(PART_OTA, PC_BOOT_OFF_OTA, PC_BOOT_SIZE_OTA, true);
    pc_part_add(PART_CONFIG, PC_BOOT_OFF_CONFIG, PC_BOOT_SIZE_CONFIG, true);
}

uint32_t boot_port_ram_start(void)
{
    return 0U;
}

uint32_t boot_port_ram_end(void)
{
    return 0U;          /* host 没有可启动的 SRAM 镜像 */
}

int boot_port_jump(uint32_t entry)
{
    (void)entry;
    return -1;          /* host 不支持跳转，测试请用 boot_set_jump() */
}

#endif /* IOT_PLATFORM_PC */
