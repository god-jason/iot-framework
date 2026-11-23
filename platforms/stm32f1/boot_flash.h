#ifndef BOARD_FLASH_H
#define BOARD_FLASH_H

/*
 * boot_flash.h - STM32F103CBT6 Flash 原语接口
 *
 * 供 boot_meta/boot_config/boot_crash 的 Meta/配置/日志/OTA 写入使用；
 * 实现见 boot_flash.c（按页擦除、半字编程）。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 擦除 addr 开始、覆盖 len 字节的区域（按页向上取整）；成功返回 0 */
int boot_flash_erase(uint32_t addr, uint32_t len);

/* 从 addr 开始写入 len 字节；成功返回 0 */
int boot_flash_write(uint32_t addr, const void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_FLASH_H */
