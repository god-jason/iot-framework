/*
 * board_uart.c - STM32F103C8 板级 trace 串口（USART1 / PA9）
 *
 * 提供 hal_uart_transmit() 的板级轮询实现，供 platforms/stm32/stm32_trace.c
 * 的 weak 引用解析；接收侧使用 USART1 中断 + 128 字节环形缓冲，
 * hal_uart_receive() 从环形缓冲取数，任务空闲时无需忙等。
 * 同时提供 board_trace_start() 完成初始化与注册。
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
#define BOARD_SR_ORE        (1UL << 3)
#define BOARD_CR1_RE        (1UL << 2)
#define BOARD_CR1_TE        (1UL << 3)
#define BOARD_CR1_RXNEIE    (1UL << 5)
#define BOARD_CR1_UE        (1UL << 13)

#define BOARD_RX_BUF_SIZE   128U

static volatile int s_uart_ready;
static ringbuf_t s_rx;
static uint8_t s_rx_mem[BOARD_RX_BUF_SIZE];

/* USART1 接收中断：把字节压入环形缓冲（任务侧按需取出） */
void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;

    if ((sr & (BOARD_SR_RXNE | BOARD_SR_ORE)) != 0U) {
        uint8_t ch = (uint8_t)(USART1->DR & 0xFFU);   /* 读 DR 清除 RXNE/ORE */

        if ((sr & BOARD_SR_RXNE) != 0U) {
            (void)ringbuf_write(&s_rx, &ch, 1U);
        }
    }
}

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

    /* PA10 -> 上拉输入（CRH bits[11:8] = 1000），用于 Shell 接收 */
    GPIOA->CRH &= ~(0x0FUL << 8);
    GPIOA->CRH |=  (0x08UL << 8);
    GPIOA->ODR |=  (1UL << 10);

    USART1->BRR = BOARD_USART_CLK_HZ / baud;
    (void)ringbuf_init(&s_rx, s_rx_mem, sizeof(s_rx_mem));
    USART1->CR1 = BOARD_CR1_RE | BOARD_CR1_TE | BOARD_CR1_UE | BOARD_CR1_RXNEIE;
    NVIC_SetPriority(USART1_IRQn, 6U);
    NVIC_EnableIRQ(USART1_IRQn);

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
 * 从接收环形缓冲取字节（中断已把数据收进来）。
 * timeout 为 0 时只做一次非阻塞检查（Shell 输入泵用）；
 * timeout > 0 时按约 72MHz 的忙等计数近似毫秒超时。
 */
int hal_uart_receive(void *handle, uint8_t *data,
                     size_t length, uint32_t timeout)
{
    size_t i;

    (void)handle;

    if (data == NULL || !s_uart_ready || length == 0U) {
        return -1;
    }

    for (i = 0U; i < length; i++) {
        uint32_t budget = timeout * (BOARD_USART_CLK_HZ / 1000U / 4U);

        while (ringbuf_read(&s_rx, &data[i], 1U) != 1U) {
            if (timeout == 0U) {
                return -1;              /* 非阻塞：无数据 */
            }
            if (budget-- == 0U) {
                return -1;              /* 超时 */
            }
        }
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
