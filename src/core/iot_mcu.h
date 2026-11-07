#ifndef IOT_MCU_H
#define IOT_MCU_H

/*
 * 平台/芯片信息与能力查询。
 * 具体平台（platforms/<platform>/）实现 iot_init() / iot_deinit() /
 * iot_get_info() / iot_get_capabilities()；所有目标仅链接一个平台实现。
 */

#include <stdint.h>

#include "iot_status.h"
#include "iot_macro.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 外设能力标志 ----------------
 * 位掩码形式，可按位或组合；由 iot_get_capabilities() 返回。
 */
typedef enum {
    IOT_CAP_DMA           = IOT_BIT(0),   /* 支持 DMA 传输 */
    IOT_CAP_IT            = IOT_BIT(1),   /* 支持中断方式传输 */
    IOT_CAP_CIRCULAR      = IOT_BIT(2),   /* 支持循环（环形）模式 */
    IOT_CAP_DOUBLE_BUFFER = IOT_BIT(3),   /* 支持双缓冲 */
    IOT_CAP_HALF_TRANSFER = IOT_BIT(4),   /* 支持半传输事件 */
    IOT_CAP_IDLE_DETECT   = IOT_BIT(5),   /* 支持总线空闲检测 */
    IOT_CAP_CACHE         = IOT_BIT(6),   /* 需要/支持缓存维护操作 */
    IOT_CAP_FIFO          = IOT_BIT(7)    /* 具备硬件 FIFO */
} iot_capability_t;

/* MCU 家族标识，用于上层做按家族的差异化处理 */
typedef enum {
    IOT_MCU_UNKNOWN = 0,       /* 未知/未识别 */
    IOT_MCU_STM32F1,           /* STM32F1 系列 */
    IOT_MCU_STM32F4,           /* STM32F4 系列 */
    IOT_MCU_STM32H7,           /* STM32H7 系列 */
    IOT_MCU_GD32,              /* 兆易创新 GD32 */
    IOT_MCU_CH32,              /* 沁恒 CH32 */
    IOT_MCU_N32,               /* 国民技术 N32 */
    IOT_MCU_AVR,               /* Atmel AVR */
    IOT_MCU_PC          /* 主机模拟器 */
} iot_mcu_family_t;

/* 芯片信息，由平台 iot_get_info() 填充 */
typedef struct {
    iot_mcu_family_t family;   /* MCU 家族 */

    uint32_t model;             /* 型号标识（平台自定义编码） */
    uint32_t revision;          /* 芯片修订版本 */

    uint32_t core_clock;        /* 内核时钟频率（Hz） */
    uint32_t ahb_clock;         /* AHB 总线频率（Hz） */
    uint32_t apb1_clock;        /* APB1 总线频率（Hz） */
    uint32_t apb2_clock;        /* APB2 总线频率（Hz） */

    uint32_t flash_size;        /* 片内 Flash 容量（字节） */
    uint32_t sram_size;         /* 片内 SRAM 容量（字节） */
} iot_mcu_info_t;

/* 设备类型，用于 iot_get_capabilities() 查询 */
typedef enum {
    IOT_DEVICE_GPIO = 0,       /* GPIO */
    IOT_DEVICE_UART,           /* UART/USART */
    IOT_DEVICE_SPI,            /* SPI */
    IOT_DEVICE_I2C,            /* I2C */
    IOT_DEVICE_CAN,            /* CAN */
    IOT_DEVICE_ADC,            /* ADC */
    IOT_DEVICE_DAC,            /* DAC */
    IOT_DEVICE_PWM,            /* PWM */
    IOT_DEVICE_TIMER,          /* 定时器 */
    IOT_DEVICE_FLASH,          /* 片内 Flash */
    IOT_DEVICE_USB,            /* USB */
    IOT_DEVICE_ETH             /* 以太网 */
} iot_device_type_t;

/*
 * 初始化 / 反初始化 PORT 层。
 * 由当前链接的平台实现提供。
 * 返回: IOT_OK 成功；其余为平台相关错误。
 */
iot_status_t iot_init(void);
iot_status_t iot_deinit(void);

/* 获取芯片信息；info 不可为 NULL */
iot_status_t iot_get_info(iot_mcu_info_t *info);

/*
 * 查询外设能力。
 * type     : 设备类型；
 * instance : 逻辑实例号，从 0 开始。
 * 返回     : IOT_CAP_* 的位掩码；实例不存在时返回 0。
 */
uint32_t iot_get_capabilities(iot_device_type_t type, uint8_t instance);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MCU_H */
