/*
 * board_uart.c - STM32F103C8 板级 trace 串口（USART1 / PA9）
 *
 * 提供 hal_uart_transmit() 的板级轮询实现，供 platforms/stm32/stm32_trace.c
 * 的 weak 引用解析；同时提供 board_trace_start() 完成初始化与注册。
 * 不依赖 STM32Cube HAL，纯寄存器操作，刷机即可在串口看到 trace 文本行。
 *
 * 若工程改用 Cube HAL 实现 hal_uart_transmit()，请删除本文件或去掉该函数，
 * 并在应用里自行调用 ros_trace_set_output() 注册句柄。
 */

#include "board.h"
#include "ros.h"
#include "common.h"

#include "stm32f10x.h"

#include <stddef.h>

#define BOARD_USART_CLK_HZ  72000000U
#define BOARD_SR_TXE        (1UL << 7)
#define BOARD_SR_RXNE       (1UL << 5)
#define BOARD_CR1_RE        (1UL << 2)
#define BOARD_CR1_TE        (1UL << 3)
#define BOARD_CR1_UE        (1UL << 13)

static volatile int s_uart_ready;

static void board_usart1_init(uint32_t baud)
{
    if (baud == 0U) {
        baud = 115200U;
    }

    /* IOPAEN | USART1EN */
    RCC->APB2ENR |= (1UL << 2) | (1UL << 14);

    /* PA9 -> 复用推挽输出 50MHz（CRH bits[7:4]） */
    GPIOA->CRH &= ~(0x0FUL << 4);
    GPIOA->CRH |=  (0x0BUL << 4);

    /* PA10 -> 上拉输入（CRL bits[11:8] = 1000），用于 Shell 接收 */
    GPIOA->CRL &= ~(0x0FUL << 8);
    GPIOA->CRL |=  (0x08UL << 8);
    GPIOA->ODR |=  (1UL << 10);

    USART1->BRR = BOARD_USART_CLK_HZ / baud;
    USART1->CR1 = BOARD_CR1_RE | BOARD_CR1_TE | BOARD_CR1_UE;

    s_uart_ready = 1;
}

int hal_uart_transmit(void *handle, const uint8_t *data,
                      size_t length, uint32_t timeout)
{
    size_t i;

    (void)handle;
    (void)timeout;

    if (data == NULL || !s_uart_ready) {
        return -1;
    }
    for (i = 0U; i < length; i++) {
        while ((USART1->SR & BOARD_SR_TXE) == 0U) {
            /* 等待发送寄存器空 */
        }
        USART1->DR = data[i];
    }
    return 0;
}

/*
 * 轮询接收：按约 72MHz 的忙等计数近似毫秒超时。
 * timeout 为 0 时只做一次非阻塞检查（Shell 输入泵用 20ms）。
 */
int hal_uart_receive(void *handle, uint8_t *data,
                     size_t length, uint32_t timeout)
{
    size_t i;
    uint32_t spin = timeout * (BOARD_USART_CLK_HZ / 1000U / 4U);

    (void)handle;

    if (data == NULL || !s_uart_ready || length == 0U) {
        return -1;
    }

    for (i = 0U; i < length; i++) {
        uint32_t budget = spin;

        while ((USART1->SR & BOARD_SR_RXNE) == 0U) {
            if (timeout == 0U) {
                return -1;              /* 非阻塞：无数据 */
            }
            if (budget-- == 0U) {
                return -1;              /* 超时 */
            }
        }
        data[i] = (uint8_t)(USART1->DR & 0xFFU);
    }
    return 0;
}

void board_trace_start(uint32_t baud)
{
    board_usart1_init(baud);

    /* 句柄仅作非空标记，板级实现不依赖具体 hal_uart_t */
    ros_trace_set_output((void *)1, 0U);

    /* 只输出核心事件，避免串口轮询影响实时性；需要全量时改 DEBUG */
    ros_trace_set_level(ROS_TRACE_LEVEL_CORE);
}
