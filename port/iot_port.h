#ifndef IOT_PORT_H
#define IOT_PORT_H

/*
 * Port 平台接口层。
 * 框架 Core / Components 不直接依赖 STM32 HAL 或具体寄存器，
 * 而是通过本文件定义的统一接口访问底层能力。
 * 具体实现由 platform/ 下的 F103 / F407 / host 提供。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 系统基础接口 ---- */
typedef struct {
    void (*init)(void);

    iot_tick_t (*tick_get)(void);
    void (*delay_ms)(uint32_t ms);
    void (*delay_us)(uint32_t us);

    void (*yield)(void);

    void (*critical_enter)(void);
    void (*critical_exit)(void);

    void (*reset)(void);
} iot_port_system_t;

/* ---- UART ---- */
typedef struct {
    int (*init)(void *ctx, uint32_t baudrate);
    int (*deinit)(void *ctx);

    int (*send)(void *ctx, const uint8_t *data, uint32_t len);
    int (*receive)(void *ctx, uint8_t *data, uint32_t len);

    int (*set_baudrate)(void *ctx, uint32_t baudrate);
    int (*flush)(void *ctx);

    uint32_t (*available)(void *ctx);

    void *ctx;
} iot_port_uart_t;

/* ---- GPIO ---- */
typedef enum {
    IOT_GPIO_INPUT          = 0,
    IOT_GPIO_OUTPUT         = 1,
    IOT_GPIO_INPUT_PULLUP   = 2,
    IOT_GPIO_INPUT_PULLDOWN = 3,
    IOT_GPIO_OUTPUT_OD      = 4
} iot_gpio_mode_t;

typedef enum {
    IOT_GPIO_LOW  = 0,
    IOT_GPIO_HIGH = 1
} iot_gpio_level_t;

typedef struct {
    int (*init)(void *ctx, iot_gpio_mode_t mode);
    int (*write)(void *ctx, iot_gpio_level_t level);
    int (*read)(void *ctx, iot_gpio_level_t *level);
    int (*toggle)(void *ctx);

    void *ctx;
} iot_port_gpio_t;

/* ---- SPI ---- */
typedef struct {
    int (*init)(void *ctx);
    int (*deinit)(void *ctx);
    int (*transfer)(void *ctx, const uint8_t *tx, uint8_t *rx, uint32_t len);
    void *ctx;
} iot_port_spi_t;

/* ---- I2C ---- */
typedef struct {
    int (*init)(void *ctx);
    int (*deinit)(void *ctx);
    int (*write)(void *ctx, uint16_t addr, const uint8_t *data, uint32_t len);
    int (*read)(void *ctx, uint16_t addr, uint8_t *data, uint32_t len);
    void *ctx;
} iot_port_i2c_t;

/* ---- ADC ---- */
typedef struct {
    int (*init)(void *ctx);
    int (*read)(void *ctx, uint16_t *value);
    int (*start)(void *ctx);
    int (*stop)(void *ctx);
    void *ctx;
} iot_port_adc_t;

/* ---- PWM ---- */
typedef struct {
    int (*init)(void *ctx, uint32_t frequency);
    int (*set_frequency)(void *ctx, uint32_t frequency);
    int (*set_duty)(void *ctx, uint16_t duty);
    int (*enable)(void *ctx);
    int (*disable)(void *ctx);
    void *ctx;
} iot_port_pwm_t;

/* ---- Flash ---- */
typedef struct {
    int (*init)(void *ctx);
    int (*read)(void *ctx, uint32_t addr, void *data, uint32_t len);
    int (*write)(void *ctx, uint32_t addr, const void *data, uint32_t len);
    int (*erase)(void *ctx, uint32_t addr, uint32_t len);
    uint32_t (*get_size)(void *ctx);
    uint32_t (*get_erase_size)(void *ctx);
    void *ctx;
} iot_port_flash_t;

/* ---- 统一 Port 集合 ---- */
typedef struct {
    iot_port_system_t system;
    /* 其他外设通过各自驱动实例注册，不在此处展开 */
} iot_port_t;

int iot_port_init(const iot_port_t *port);
const iot_port_t *iot_port_get(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_PORT_H */