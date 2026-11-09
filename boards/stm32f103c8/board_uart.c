/*
 * board_uart.c - STM32F103C8 板级 trace 串口（USART1 / PA9）
 *
 * 提供 hal_uart_transmit() 的板级轮询实现，供 platforms/stm32/stm32_trace.c
 * 的 weak 引用解析；同时提供 board_trace_start() 完成初始化与注册。
 * 不依赖 STM32Cube HAL，纯寄存器操作，刷机即可在串口看到 trace 文本行。
 *
 * 若工程改用 Cube HAL 实现 hal_uart_transmit()，请删除本文件或去掉该函数，
 * 并在应用里自行调用 iot_trace_set_output() 注册句柄。
 */

#include "board.h"
#include "iot.h"

#include "stm32f10x.h"

#include <stddef.h>

#define BOARD_USART_CLK_HZ  72000000U
#define BOARD_SR_TXE        (1UL << 7)
#define BOARD_CR1_TE        (1UL << 3)
#define BOARD_CR1_UE        (1UL << 13)

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

    USART1->BRR = BOARD_USART_CLK_HZ / baud;
    USART1->CR1 = BOARD_CR1_TE | BOARD_CR1_UE;
}

int hal_uart_transmit(void *handle, const uint8_t *data,
                      size_t length, uint32_t timeout)
{
    size_t i;

    (void)handle;
    (void)timeout;

    if (data == NULL) {
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

void board_trace_start(uint32_t baud)
{
    board_usart1_init(baud);

    /* 句柄仅作非空标记，板级实现不依赖具体 hal_uart_t */
    iot_trace_set_output((void *)1, 0U);

    /* 只输出核心事件，避免串口轮询影响实时性；需要全量时改 DEBUG */
    iot_trace_set_level(IOT_TRACE_LEVEL_CORE);
}
