#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

/*
 * 平台注册接口。
 * 每个平台（ports/<platform>/）实现 port_platform_install()，
 * 在其中调用 port_platform_register() 注册 Platform Ops。
 * port_init() 会在尚未注册时自动调用 port_platform_install()。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_MCU_UNKNOWN = 0,
    PORT_MCU_STM32F1,
    PORT_MCU_STM32F4,
    PORT_MCU_STM32H7,
    PORT_MCU_GD32,
    PORT_MCU_CH32,
    PORT_MCU_N32,
    PORT_MCU_AVR,
    PORT_MCU_SIMULATOR
} port_mcu_family_t;

typedef struct {
    port_mcu_family_t family;

    uint32_t model;
    uint32_t revision;

    uint32_t core_clock;
    uint32_t ahb_clock;
    uint32_t apb1_clock;
    uint32_t apb2_clock;

    uint32_t flash_size;
    uint32_t sram_size;
} port_mcu_info_t;

typedef struct {
    const char *name;

    port_mcu_family_t family;

    port_status_t (*init)(void);
    port_status_t (*deinit)(void);

    port_status_t (*get_info)(port_mcu_info_t *info);
} port_platform_ops_t;

/* 平台注册 / 获取 */
port_status_t port_platform_register(const port_platform_ops_t *ops);
const port_platform_ops_t *port_platform_get(void);
const char *port_platform_name(void);

/*
 * 由平台实现：注册自身 Platform Ops。
 * 所有目标必须链接且仅链接一个平台实现。
 */
port_status_t port_platform_install(void);

/* 能力查询：按设备类型 + 逻辑实例号返回 PORT_CAP_* 位掩码 */
typedef enum {
    PORT_DEVICE_GPIO = 0,
    PORT_DEVICE_UART,
    PORT_DEVICE_SPI,
    PORT_DEVICE_I2C,
    PORT_DEVICE_CAN,
    PORT_DEVICE_ADC,
    PORT_DEVICE_DAC,
    PORT_DEVICE_PWM,
    PORT_DEVICE_TIMER,
    PORT_DEVICE_FLASH,
    PORT_DEVICE_USB,
    PORT_DEVICE_ETH
} port_device_type_t;

uint32_t port_get_capabilities(port_device_type_t type, uint8_t instance);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PLATFORM_H */
