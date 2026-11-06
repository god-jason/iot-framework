/* MCU 硬件驱动 host 模拟实现。
 * host 下用静态状态模拟外设；真实 MCU 将对应函数映射到寄存器 / STM32 HAL。 */
#include "iot_mcu.h"
#include "iot_logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- GPIO 模拟状态 ---- */
#define MCU_GPIO_MAX 32
static uint8_t s_gpio_level[MCU_GPIO_MAX];
static uint8_t s_gpio_mode[MCU_GPIO_MAX];

int iot_mcu_gpio_set_mode(uint8_t pin, uint8_t mode)
{
    if (pin >= MCU_GPIO_MAX) return IOT_ERR_PARAM;
    s_gpio_mode[pin] = mode;
    return IOT_OK;   /* log: gpio mode set */
}
int iot_mcu_gpio_write(uint8_t pin, uint8_t level)
{
    if (pin >= MCU_GPIO_MAX) return IOT_ERR_PARAM;
    s_gpio_level[pin] = level ? 1 : 0;
    return IOT_OK;
}
int iot_mcu_gpio_read(uint8_t pin, uint8_t *level)
{
    if (pin >= MCU_GPIO_MAX || level == NULL) return IOT_ERR_PARAM;
    *level = s_gpio_level[pin];
    return IOT_OK;
}
int iot_mcu_gpio_toggle(uint8_t pin)
{
    if (pin >= MCU_GPIO_MAX) return IOT_ERR_PARAM;
    s_gpio_level[pin] ^= 1;
    return IOT_OK;
}

/* ---- UART 模拟：接收缓冲由测试注入 ---- */
#define MCU_UART_MAX 4
static uint8_t s_uart_rx[MCU_UART_MAX][64];   /* host 模拟接收缓冲 */
static uint8_t s_uart_len[MCU_UART_MAX];

int iot_mcu_uart_init(uint8_t id, uint32_t baudrate)
{
    (void)baudrate;
    if (id >= MCU_UART_MAX) return IOT_ERR_PARAM;
    memset(s_uart_rx[id], 0, sizeof(s_uart_rx[id]));
    s_uart_len[id] = 0;
    return IOT_OK;
}
int iot_mcu_uart_write(uint8_t id, const uint8_t *data, uint32_t len)
{
    (void)id; (void)data; (void)len;
    return IOT_OK;   /* host: uart tx noop */
}
int iot_mcu_uart_read(uint8_t id, uint8_t *data, uint32_t len)
{
    if (id >= MCU_UART_MAX || data == NULL) return IOT_ERR_PARAM;
    if (len > s_uart_len[id]) len = s_uart_len[id];
    memcpy(data, s_uart_rx[id], len);
    s_uart_len[id] = 0;
    return (int)len;
}

/* ---- TIMER / CLOCK ---- */
static uint32_t s_tick = 0;
int iot_mcu_timer_init(void) { s_tick = 0; return IOT_OK; }
uint32_t iot_mcu_tick_ms(void) { return s_tick++; }   /* host: tick++ 模拟 1ms */
uint32_t iot_mcu_clock_hz(void) { return 72000000u; }

/* ---- PWM ---- */
#define MCU_PWM_MAX 8
static uint16_t s_pwm_duty[MCU_PWM_MAX];
int iot_mcu_pwm_init(uint8_t channel, uint32_t frequency)
{
    (void)frequency;
    if (channel >= MCU_PWM_MAX) return IOT_ERR_PARAM;
    s_pwm_duty[channel] = 0;
    return IOT_OK;
}
int iot_mcu_pwm_set_duty(uint8_t channel, uint16_t duty)
{
    if (channel >= MCU_PWM_MAX || duty > 1000) return IOT_ERR_PARAM;
    s_pwm_duty[channel] = duty;
    return IOT_OK;
}

/* ---- ADC ---- */
#define MCU_ADC_MAX 8
static uint16_t s_adc_value[MCU_ADC_MAX];
int iot_mcu_adc_init(uint8_t channel)
{
    if (channel >= MCU_ADC_MAX) return IOT_ERR_PARAM;
    s_adc_value[channel] = 0;
    return IOT_OK;
}
int iot_mcu_adc_read(uint8_t channel, uint16_t *value)
{
    if (channel >= MCU_ADC_MAX || value == NULL) return IOT_ERR_PARAM;
    *value = s_adc_value[channel];
    return IOT_OK;
}

/* ---- I2C 模拟寄存器 ---- */
static uint8_t s_i2c_reg[128];
int iot_mcu_i2c_init(void) { memset(s_i2c_reg, 0, sizeof(s_i2c_reg)); return IOT_OK; }
int iot_mcu_i2c_write(uint8_t addr, const uint8_t *data, uint32_t len)
{
    (void)addr;
    if (data == NULL || len > sizeof(s_i2c_reg)) return IOT_ERR_PARAM;
    memcpy(s_i2c_reg, data, len);
    return IOT_OK;
}
int iot_mcu_i2c_read(uint8_t addr, uint8_t *data, uint32_t len)
{
    (void)addr;
    if (data == NULL || len > sizeof(s_i2c_reg)) return IOT_ERR_PARAM;
    memcpy(data, s_i2c_reg, len);
    return IOT_OK;
}

/* ---- SPI 回环模拟 ---- */
int iot_mcu_spi_init(void) { return IOT_OK; }
int iot_mcu_spi_transfer(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    if (tx == NULL || rx == NULL) return IOT_ERR_PARAM;
    memcpy(rx, tx, len);   /* host: loopback */
    return IOT_OK;
}

/* ---- FLASH 模拟 ---- */
#define MCU_FLASH_SIZE 0x10000
static uint8_t s_flash[MCU_FLASH_SIZE];
int iot_mcu_flash_init(void) { memset(s_flash, 0xFF, sizeof(s_flash)); return IOT_OK; }
int iot_mcu_flash_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    if (data == NULL || addr + len > MCU_FLASH_SIZE) return IOT_ERR_PARAM;
    memcpy(data, &s_flash[addr], len);
    return IOT_OK;
}
int iot_mcu_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (data == NULL || addr + len > MCU_FLASH_SIZE) return IOT_ERR_PARAM;
    memcpy(&s_flash[addr], data, len);
    return IOT_OK;
}
int iot_mcu_flash_erase(uint32_t addr, uint32_t len)
{
    if (addr + len > MCU_FLASH_SIZE) return IOT_ERR_PARAM;
    memset(&s_flash[addr], 0xFF, len);
    return IOT_OK;
}

/* ---- CAN 模拟 ---- */
static iot_mcu_can_rx_fn s_can_rx = NULL;
static void *s_can_user = NULL;
int iot_mcu_can_init(uint32_t baud) { (void)baud; return IOT_OK; }
int iot_mcu_can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    (void)id; (void)data; (void)len;
    return IOT_OK;
}
int iot_mcu_can_set_rx(iot_mcu_can_rx_fn cb, void *user)
{
    s_can_rx = cb;
    s_can_user = user;
    return IOT_OK;
}

/* ---- LOG ---- */
void iot_mcu_log(const char *tag, const char *fmt, ...)
{
    char buf[128];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    iot_log_write(IOT_LOG_INFO, tag, "%s", buf);
}