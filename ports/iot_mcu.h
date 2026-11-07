#ifndef IOT_MCU_H
#define IOT_MCU_H

/*
 * MCU 硬件驱动封装层（host 可模拟）。
 * 每个外设提供统一函数接口；host 下用静态状态模拟，真实 MCU 由 platform 层
 * 将对应函数映射到寄存器 / STM32 HAL / SDK。所有底层驱动不动态分配内存。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- GPIO ---------------- */
int iot_mcu_gpio_set_mode(uint8_t pin, uint8_t mode);   /* 0=in 1=out */
int iot_mcu_gpio_write(uint8_t pin, uint8_t level);
int iot_mcu_gpio_read(uint8_t pin, uint8_t *level);
int iot_mcu_gpio_toggle(uint8_t pin);

/* ---------------- UART ---------------- */
int iot_mcu_uart_init(uint8_t id, uint32_t baudrate);
int iot_mcu_uart_write(uint8_t id, const uint8_t *data, uint32_t len);
int iot_mcu_uart_read(uint8_t id, uint8_t *data, uint32_t len);

/* ---------------- TIMER / CLOCK ---------------- */
int iot_mcu_timer_init(void);
uint32_t iot_mcu_tick_ms(void);        /* 单调 ms tick */
uint32_t iot_mcu_clock_hz(void);       /* 系统主频 */

/* ---------------- PWM ---------------- */
int iot_mcu_pwm_init(uint8_t channel, uint32_t frequency);
int iot_mcu_pwm_set_duty(uint8_t channel, uint16_t duty);  /* 0..1000 = 0..100% */

/* ---------------- ADC ---------------- */
int iot_mcu_adc_init(uint8_t channel);
int iot_mcu_adc_read(uint8_t channel, uint16_t *value);

/* ---------------- I2C ---------------- */
int iot_mcu_i2c_init(void);
int iot_mcu_i2c_write(uint8_t addr, const uint8_t *data, uint32_t len);
int iot_mcu_i2c_read(uint8_t addr, uint8_t *data, uint32_t len);

/* ---------------- SPI ---------------- */
int iot_mcu_spi_init(void);
int iot_mcu_spi_transfer(const uint8_t *tx, uint8_t *rx, uint32_t len);

/* ---------------- FLASH ---------------- */
int iot_mcu_flash_init(void);
int iot_mcu_flash_read(uint32_t addr, uint8_t *data, uint32_t len);
int iot_mcu_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);
int iot_mcu_flash_erase(uint32_t addr, uint32_t len);

/* ---------------- CAN ---------------- */
typedef void (*iot_mcu_can_rx_fn)(uint32_t id, const uint8_t *data, uint8_t len, void *user);
int iot_mcu_can_init(uint32_t baud);
int iot_mcu_can_send(uint32_t id, const uint8_t *data, uint8_t len);
int iot_mcu_can_set_rx(iot_mcu_can_rx_fn cb, void *user);

/* ---------------- LOG ---------------- */
/* 统一日志宏：输出到注册的 logger 后端（英文简短消息） */
void iot_mcu_log(const char *tag, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* IOT_MCU_H */