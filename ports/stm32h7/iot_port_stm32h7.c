/* STM32H743 平台适配实现：iot_port 接口 -> STM32H7 HAL。
 * 仅 STM32H7 交叉编译目标参与构建，host 不编译。 */
#include "iot_port_stm32h7.h"
#include "stm32h7xx_hal.h"

#include <string.h>

extern UART_HandleTypeDef huart1;
extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c1;

/* ---- system ---- */
static iot_tick_t h7_tick(void) { return (iot_tick_t)HAL_GetTick(); }
static void h7_delay_ms(uint32_t ms) { HAL_Delay(ms); }
static void h7_delay_us(uint32_t us) { (void)us; }
static void h7_critical_enter(void) { __disable_irq(); }
static void h7_critical_exit(void) { __enable_irq(); }
static void h7_reset(void) { NVIC_SystemReset(); }

/* ---- UART1 ---- */
static int h7_uart_send(void *ctx, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    return (HAL_UART_Transmit(&huart1, (uint8_t *)data, (uint16_t)len, 1000) == HAL_OK)
           ? (int)len : -1;
}

static int h7_uart_receive(void *ctx, uint8_t *data, uint32_t len)
{
    (void)ctx;
    return (HAL_UART_Receive(&huart1, data, (uint16_t)len, 1000) == HAL_OK)
           ? (int)len : -1;
}

static uint32_t h7_uart_available(void *ctx)
{
    (void)ctx;
    return 0;   /* 阻塞模式；中断/DMA 需维护 RingBuffer */
}

/* ---- SPI1 ---- */
static int h7_spi_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    (void)ctx;
    return (HAL_SPI_TransmitReceive(&hspi1, (uint8_t *)tx, rx,
                                    (uint16_t)len, 1000) == HAL_OK) ? 0 : -1;
}

/* ---- I2C1 ---- */
static int h7_i2c_write(void *ctx, uint16_t addr, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    return (HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(addr << 1),
                                    (uint8_t *)data, (uint16_t)len, 1000) == HAL_OK)
           ? 0 : -1;
}

static int h7_i2c_read(void *ctx, uint16_t addr, uint8_t *data, uint32_t len)
{
    (void)ctx;
    return (HAL_I2C_Master_Receive(&hi2c1, (uint16_t)((addr << 1) | 1),
                                   data, (uint16_t)len, 1000) == HAL_OK) ? 0 : -1;
}

static iot_port_t h7_port = {
    { NULL, h7_tick, h7_delay_ms, h7_delay_us, NULL,
      h7_critical_enter, h7_critical_exit, h7_reset }
};

static iot_port_uart_t h7_uart1 = {
    NULL, NULL, h7_uart_send, h7_uart_receive, NULL, NULL, h7_uart_available, NULL
};

static iot_port_spi_t h7_spi1 = { NULL, NULL, h7_spi_transfer, NULL };
static iot_port_i2c_t h7_i2c1 = { NULL, NULL, h7_i2c_write, h7_i2c_read, NULL };

int iot_stm32h7_port_init(void)
{
    return iot_port_init(&h7_port);
}

const iot_port_t *iot_stm32h7_port_get(void) { return &h7_port; }
const iot_port_uart_t *iot_stm32h7_uart1_get(void) { return &h7_uart1; }
const iot_port_spi_t *iot_stm32h7_spi1_get(void) { return &h7_spi1; }
const iot_port_i2c_t *iot_stm32h7_i2c1_get(void) { return &h7_i2c1; }