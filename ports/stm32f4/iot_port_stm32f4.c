/* STM32F407 平台适配实现：iot_port 接口 -> STM32F4 HAL。
 * 注意：仅 STM32 交叉编译目标参与构建，host 不编译。 */
#include "iot_port_stm32f4.h"
#include "stm32f4xx_hal.h"

#include <string.h>

extern UART_HandleTypeDef huart1;

/* ---- system ---- */
static iot_tick_t f4_tick(void) { return (iot_tick_t)HAL_GetTick(); }
static void f4_delay_ms(uint32_t ms) { HAL_Delay(ms); }
static void f4_delay_us(uint32_t us) { (void)us; }
static void f4_critical_enter(void) { __disable_irq(); }
static void f4_critical_exit(void) { __enable_irq(); }
static void f4_reset(void) { NVIC_SystemReset(); }

/* ---- UART1 ---- */
static int f4_uart_send(void *ctx, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    return (HAL_UART_Transmit(&huart1, (uint8_t *)data, (uint16_t)len, 1000) == HAL_OK)
           ? (int)len : -1;
}

static int f4_uart_receive(void *ctx, uint8_t *data, uint32_t len)
{
    (void)ctx;
    return (HAL_UART_Receive(&huart1, data, (uint16_t)len, 1000) == HAL_OK)
           ? (int)len : -1;
}

static uint32_t f4_uart_available(void *ctx)
{
    (void)ctx;
    return 0;   /* 阻塞模式；中断/DMA 需维护 RingBuffer 返回可读字节数 */
}

/* ---- GPIO ---- */
static int f4_gpio_write(void *ctx, iot_gpio_level_t level)
{
    GPIO_TypeDef *port = (GPIO_TypeDef *)ctx;
    uint16_t pin = 0;   // 由封装结构取 pin
    HAL_GPIO_WritePin(port, pin, (GPIO_PinState)level);
    return IOT_OK;
}

static iot_port_system_t f4_system = {
    NULL, f4_tick, f4_delay_ms, f4_delay_us, NULL,
    f4_critical_enter, f4_critical_exit, f4_reset
};

static iot_port_uart_t f4_uart1 = {
    NULL, NULL, f4_uart_send, f4_uart_receive, NULL, NULL, f4_uart_available, NULL
};

static iot_port_t f4_port = { { NULL, f4_tick, f4_delay_ms, f4_delay_us, NULL,
    f4_critical_enter, f4_critical_exit, f4_reset } };

int iot_stm32f4_port_init(void)
{
    return iot_port_init(&f4_port);
}

const iot_port_t *iot_stm32f4_port_get(void) { return &f4_port; }
const iot_port_uart_t *iot_stm32f4_uart1_get(void) { return &f4_uart1; }