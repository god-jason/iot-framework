#ifndef BOARD_FLASH_H
#define BOARD_FLASH_H

/*
 * board_flash.h - STM32F103CBT6 Flash 原语接口
 *
 * 供 KV/错误日志/OTA/App 的 Flash 写入使用；
 * 实现见 board_flash.c（按页擦除、半字编程）。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 擦除 addr 开始、覆盖 len 字节的区域（按页向上取整）；成功返回 0 */
int board_flash_erase(uint32_t addr, uint32_t len);

/* 从 addr 开始写入 len 字节；成功返回 0 */
int board_flash_write(uint32_t addr, const void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_FLASH_H */
