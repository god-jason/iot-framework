/* STM32F103 平台适配实现：iot_port 接口 -> STM32F1 HAL。
 * 注意：本文件依赖 STM32CubeF1 HAL，仅在 STM32 交叉编译目标中参与构建。 */
#include "iot_port_stm32f1.h"
#include "stm32f1xx_hal.h"

#include <string.h>

/* 由 CubeMX 生成的 USART1 句柄（外部声明） */
extern UART_HandleTypeDef huart1;

/* ---- system ---- */
static iot_tick_t f1_tick(void) { return (iot_tick_t)HAL_GetTick(); }
static void f1_delay_ms(uint32_t ms) { HAL_Delay(ms); }
static void f1_delay_us(uint32_t us) { (void)us; /* 需用 DWT/定时器实现 */ }
static void f1_critical_enter(void) { __disable_irq(); }
static void f1_critical_exit(void) { __enable_irq(); }
static void f1_reset(void) { NVIC_SystemReset(); }

/* ---- UART1 ---- */
static int f1_uart_send(void *ctx, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    if (HAL_UART_Transmit(&huart1, (uint8_t *)data, (uint16_t)len, 1000) == HAL_OK) {
        return (int)len;
    }
    return -1;
}

static int f1_uart_receive(void *ctx, uint8_t *data, uint32_t len)
{
    (void)ctx;
    if (HAL_UART_Receive(&huart1, data, (uint16_t)len, 1000) == HAL_OK) {
        return (int)len;
    }
    return -1;
}

static uint32_t f1_uart_available(void *ctx)
{
    (void)ctx;
    /* 使用 DMA+RingBuffer 时返回可读字节数；此处阻塞模式返回 0 */
    return 0;
}

/* ---- GPIO ---- */
static int f1_gpio_write(void *ctx, iot_gpio_level_t level)
{
    GPIO_TypeDef *port = (GPIO_TypeDef *)ctx;   /* ctx = {(GPIO_TypeDef*)port, uint16_t pin} */
    uint16_t pin = 0; // 由封装结构取 pin，此处简化
    HAL_GPIO_WritePin(port, pin, (GPIO_PinState)level);
    return IOT_OK;
}

/* ---- 组装端口对象 ---- */
static iot_port_system_t f1_system = {
    NULL, f1_tick, f1_delay_ms, f1_delay_us, NULL,
    f1_critical_enter, f1_critical_exit, f1_reset
};

static iot_port_uart_t f1_uart1 = {
    NULL, NULL, f1_uart_send, f1_uart_receive, NULL, NULL, f1_uart_available, NULL
};

static iot_port_t f1_port = { { NULL, f1_tick, f1_delay_ms, f1_delay_us, NULL,
    f1_critical_enter, f1_critical_exit, f1_reset } };

int iot_stm32f1_port_init(void)
{
    /* 时钟/GPIO/USART1 初始化由 CubeMX 生成的 MX_* 完成，此处置位 port */
    return iot_port_init(&f1_port);
}

const iot_port_t *iot_stm32f1_port_get(void) { return &f1_port; }
const iot_port_uart_t *iot_stm32f1_uart1_get(void) { return &f1_uart1; }