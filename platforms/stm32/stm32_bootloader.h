#ifndef STM32_BOOTLOADER_H
#define STM32_BOOTLOADER_H

/*
 * stm32_bootloader.h - STM32 平台侧的 Bootloader 移植层
 *
 * 分区、镜像、升级、串口协议等公共逻辑在 bootloader/ 目录，
 * 本文件只声明 STM32 专属能力：hal_uart 绑定与默认 Flash 布局参数。
 */

#include "bootloader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 默认 64KB Flash 布局（相对 Flash 起始的偏移，可在工程内覆盖） */
#ifndef STM32_BOOT_OFF_BOOT
#define STM32_BOOT_OFF_BOOT    0x00000U
#endif
#ifndef STM32_BOOT_SIZE_BOOT
#define STM32_BOOT_SIZE_BOOT   0x04000U
#endif
#ifndef STM32_BOOT_OFF_APP
#define STM32_BOOT_OFF_APP     0x04000U
#endif
#ifndef STM32_BOOT_SIZE_APP
#define STM32_BOOT_SIZE_APP    0x07000U
#endif
#ifndef STM32_BOOT_OFF_OTA
#define STM32_BOOT_OFF_OTA     0x0B000U
#endif
#ifndef STM32_BOOT_SIZE_OTA
#define STM32_BOOT_SIZE_OTA    0x04000U
#endif
#ifndef STM32_BOOT_OFF_CONFIG
#define STM32_BOOT_OFF_CONFIG  0x0F000U
#endif
#ifndef STM32_BOOT_SIZE_CONFIG
#define STM32_BOOT_SIZE_CONFIG 0x01000U
#endif

/* 跳转前校验栈顶的 SRAM 范围（F103C8：20KB） */
#ifndef STM32_BOOT_RAM_START
#define STM32_BOOT_RAM_START   0x20000000UL
#endif
#ifndef STM32_BOOT_RAM_END
#define STM32_BOOT_RAM_END     0x20005000UL
#endif

/*
 * 串口升级端口：直接使用已初始化的 hal_uart_t 句柄（阻塞收发）。
 * 未启用 Cube HAL 的工程可改用 boot_set_uart() 注入自己的收发实现。
 */
void boot_set_uart_handle(void *uart, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* STM32_BOOTLOADER_H */
