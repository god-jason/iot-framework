#ifndef HAL_MCU_H
#define HAL_MCU_H

/*
 * 平台/芯片信息与能力查询。
 * 具体平台（platforms/<platform>/）实现 hal_init() / hal_deinit() /
 * hal_get_info() / hal_get_capabilities()；所有目标仅链接一个平台实现。
 */

#include <stdint.h>

#include "hal_status.h"
#include "hal_macro.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 外设能力标志 ----------------
 * 位掩码形式，可按位或组合；由 hal_get_capabilities() 返回。
 */
typedef enum {
    HAL_CAP_DMA           = HAL_BIT(0),   /* 支持 DMA 传输 */
    HAL_CAP_IT            = HAL_BIT(1),   /* 支持中断方式传输 */
    HAL_CAP_CIRCULAR      = HAL_BIT(2),   /* 支持循环（环形）模式 */
    HAL_CAP_DOUBLE_BUFFER = HAL_BIT(3),   /* 支持双缓冲 */
    HAL_CAP_HALF_TRANSFER = HAL_BIT(4),   /* 支持半传输事件 */
    HAL_CAP_IDLE_DETECT   = HAL_BIT(5),   /* 支持总线空闲检测 */
    HAL_CAP_CACHE         = HAL_BIT(6),   /* 需要/支持缓存维护操作 */
    HAL_CAP_FIFO          = HAL_BIT(7)    /* 具备硬件 FIFO */
} hal_capability_t;

/* MCU 家族标识，用于上层做按家族的差异化处理 */
typedef enum {
    HAL_MCU_UNKNOWN = 0,       /* 未知/未识别 */
    HAL_MCU_STM32F1,           /* STM32F1 系列 */
    HAL_MCU_STM32F4,           /* STM32F4 系列 */
    HAL_MCU_STM32H7,           /* STM32H7 系列 */
    HAL_MCU_GD32,              /* 兆易创新 GD32 */
    HAL_MCU_CH32,              /* 沁恒 CH32 */
    HAL_MCU_N32,               /* 国民技术 N32 */
    HAL_MCU_AVR,               /* Atmel AVR */
    HAL_MCU_PC          /* 主机模拟器 */
} hal_mcu_family_t;

/* 芯片信息，由平台 hal_get_info() 填充 */
typedef struct {
    hal_mcu_family_t family;   /* MCU 家族 */

    uint32_t model;             /* 型号标识（平台自定义编码） */
    uint32_t revision;          /* 芯片修订版本 */

    uint32_t core_clock;        /* 内核时钟频率（Hz） */
    uint32_t ahb_clock;         /* AHB 总线频率（Hz） */
    uint32_t apb1_clock;        /* APB1 总线频率（Hz） */
    uint32_t apb2_clock;        /* APB2 总线频率（Hz） */

    uint32_t flash_size;        /* 片内 Flash 容量（字节） */
    uint32_t sram_size;         /* 片内 SRAM 容量（字节） */
} hal_mcu_info_t;

/* 设备类型，用于 hal_get_capabilities() 查询 */
typedef enum {
    HAL_DEVICE_GPIO = 0,       /* GPIO */
    HAL_DEVICE_UART,           /* UART/USART */
    HAL_DEVICE_SPI,            /* SPI */
    HAL_DEVICE_I2C,            /* I2C */
    HAL_DEVICE_CAN,            /* CAN */
    HAL_DEVICE_ADC,            /* ADC */
    HAL_DEVICE_DAC,            /* DAC */
    HAL_DEVICE_PWM,            /* PWM */
    HAL_DEVICE_TIMER,          /* 定时器 */
    HAL_DEVICE_FLASH,          /* 片内 Flash */
    HAL_DEVICE_USB,            /* USB */
    HAL_DEVICE_ETH             /* 以太网 */
} hal_device_type_t;

/*
 * 初始化 / 反初始化 PORT 层。
 * 由当前链接的平台实现提供。
 * 返回: HAL_OK 成功；其余为平台相关错误。
 */
hal_status_t hal_init(void);
hal_status_t hal_deinit(void);

/* 获取芯片信息；info 不可为 NULL */
hal_status_t hal_get_info(hal_mcu_info_t *info);

/*
 * 查询外设能力。
 * type     : 设备类型；
 * instance : 逻辑实例号，从 0 开始。
 * 返回     : HAL_CAP_* 的位掩码；实例不存在时返回 0。
 */
uint32_t hal_get_capabilities(hal_device_type_t type, uint8_t instance);

#ifdef __cplusplus
}
#endif

#endif /* HAL_MCU_H */
