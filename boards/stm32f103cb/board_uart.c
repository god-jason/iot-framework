/*
 * board_uart.c - STM32F103CBT6 板级串口
 *
 *   USART1 (PA9 TX / PA10 RX)：trace 与 roshell；
 *   USART2 (PA2 TX / PA3 RX)：ML307 4G 模组 AT 通道。
 *
 * 纯寄存器实现，接收使用中断 + 环形缓冲；hal_uart_transmit/receive
 * 按句柄区分串口（&s_uart2 走 USART2，其余走 USART1）。
 *
 * 注意：PA2/PA3 同时被 board_pwm_register 的 TIM2_CH3/CH4 占用，
 *       使用 ML307 时编号 2/3 的 PWM 不可用。
 */

#include "board.h"
#include "ros.h"
#include "common.h"
#include "hal.h"
#include "hal_mcu.h"

#include "stm32f10x.h"

#include <stddef.h>

#define BOARD_APB2_CLK_HZ   72000000U
#define BOARD_APB1_CLK_HZ   36000000U
#define BOARD_SR_TXE        (1UL << 7)
#define BOARD_SR_RXNE       (1UL << 5)
#define BOARD_SR_ORE        (1UL << 3)
#define BOARD_CR1_RE        (1UL << 2)
#define BOARD_CR1_TE        (1UL << 3)
#define BOARD_CR1_RXNEIE    (1UL << 5)
#define BOARD_CR1_UE        (1UL << 13)

#define BOARD_RX_BUF_SIZE   128U

/* ---------------------------- USART1 ---------------------------- */

static volatile int s_uart_ready;
static ringbuf_t s_rx;
static uint8_t s_rx_mem[BOARD_RX_BUF_SIZE];

void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;

    if ((sr & (BOARD_SR_RXNE | BOARD_SR_ORE)) != 0U) {
        uint8_t ch = (uint8_t)(USART1->DR & 0xFFU);

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

    /* PA10 -> 上拉输入（CRH bits[11:8] = 1000） */
    GPIOA->CRH &= ~(0x0FUL << 8);
    GPIOA->CRH |=  (0x08UL << 8);
    GPIOA->ODR |=  (1UL << 10);

    USART1->BRR = BOARD_APB2_CLK_HZ / baud;
    (void)ringbuf_init(&s_rx, s_rx_mem, sizeof(s_rx_mem));
    USART1->CR1 = BOARD_CR1_RE | BOARD_CR1_TE | BOARD_CR1_UE | BOARD_CR1_RXNEIE;
    NVIC_SetPriority(USART1_IRQn, 6U);
    NVIC_EnableIRQ(USART1_IRQn);

    s_uart_ready = 1;
}

/* ---------------------------- USART2（ML307） ---------------------------- */

static volatile int s_uart2_ready;
static ringbuf_t s_rx2;
static uint8_t s_rx2_mem[BOARD_RX_BUF_SIZE];
static hal_uart_t s_uart2;

void USART2_IRQHandler(void)
{
    uint32_t sr = USART2->SR;

    if ((sr & (BOARD_SR_RXNE | BOARD_SR_ORE)) != 0U) {
        uint8_t ch = (uint8_t)(USART2->DR & 0xFFU);

        if ((sr & BOARD_SR_RXNE) != 0U) {
            (void)ringbuf_write(&s_rx2, &ch, 1U);
        }
    }
}

static void board_usart2_init(uint32_t baud)
{
    if (baud == 0U) {
        baud = 115200U;
    }

    RCC->APB2ENR |= (1UL << 2);         /* IOPAEN */
    RCC->APB1ENR |= (1UL << 17);        /* USART2EN */

    /* PA2 -> 复用推挽输出 50MHz（CRL bits[11:8]） */
    GPIOA->CRL &= ~(0x0FUL << 8);
    GPIOA->CRL |=  (0x0BUL << 8);

    /* PA3 -> 上拉输入（CRL bits[15:12] = 1000） */
    GPIOA->CRL &= ~(0x0FUL << 12);
    GPIOA->CRL |=  (0x08UL << 12);
    GPIOA->ODR |=  (1UL << 3);

    USART2->BRR = BOARD_APB1_CLK_HZ / baud;
    (void)ringbuf_init(&s_rx2, s_rx2_mem, sizeof(s_rx2_mem));
    USART2->CR1 = BOARD_CR1_RE | BOARD_CR1_TE | BOARD_CR1_UE | BOARD_CR1_RXNEIE;
    NVIC_SetPriority(USART2_IRQn, 6U);
    NVIC_EnableIRQ(USART2_IRQn);

    s_uart2_ready = 1;
}

/* USART2 句柄：首次获取时初始化硬件（ML307 默认 115200） */
hal_uart_t *board_uart2(void)
{
    static bool inited;

    if (!inited) {
        board_usart2_init(115200U);
        inited = true;
    }
    return &s_uart2;
}

/* ---------------------------- HAL 适配 ---------------------------- */

static int usart_transmit(USART_TypeDef *u, const uint8_t *data, size_t length)
{
    size_t i;

    for (i = 0U; i < length; i++) {
        uint32_t spin = 2000000U;

        /* 有界等待，USART 时钟/引脚异常时不至于死等 */
        while ((u->SR & BOARD_SR_TXE) == 0U) {
            if (--spin == 0U) {
                return -1;
            }
        }
        u->DR = data[i];
    }
    return 0;
}

static int usart_receive(ringbuf_t *rb, uint8_t *data, size_t length,
                         uint32_t timeout)
{
    uint32_t start = hal_tick_get();
    size_t i;

    for (i = 0U; i < length; i++) {
        while (ringbuf_read(rb, &data[i], 1U) != 1U) {
            if (timeout == 0U) {
                return -1;              /* 非阻塞：无数据 */
            }
            if ((uint32_t)(hal_tick_get() - start) >= timeout) {
                return -1;              /* 超时 */
            }
        }
    }
    return 0;
}

int hal_uart_transmit(hal_uart_t *handle, const uint8_t *data,
                      size_t length, uint32_t timeout)
{
    (void)timeout;

    if (data == NULL) {
        return -1;
    }
    if (handle == &s_uart2) {
        if (!s_uart2_ready) {
            return -1;
        }
        return usart_transmit(USART2, data, length);
    }
    if (!s_uart_ready) {
        return -1;
    }
    return usart_transmit(USART1, data, length);
}

size_t hal_uart_get_rx_count(hal_uart_t *handle)
{
    if (handle == &s_uart2) {
        return (size_t)ringbuf_available(&s_rx2);
    }
    return (size_t)ringbuf_available(&s_rx);
}

int hal_uart_receive(hal_uart_t *handle, uint8_t *data,
                     size_t length, uint32_t timeout)
{
    ringbuf_t *rb;
    int ready;

    if (data == NULL || length == 0U) {
        return -1;
    }
    if (handle == &s_uart2) {
        rb = &s_rx2;
        ready = s_uart2_ready;
    } else {
        rb = &s_rx;
        ready = s_uart_ready;
    }
    if (!ready) {
        return -1;
    }
    return usart_receive(rb, data, length, timeout);
}

void board_trace_start(uint32_t baud)
{
    board_usart1_init(baud);

    /* 句柄仅作非空标记，板级实现不依赖具体 hal_uart_t */
    ros_trace_set_output((void *)1, 0U);

    /* 只输出核心事件，避免串口轮询影响实时性；需要全量时改 DEBUG */
    ros_trace_set_level(ROS_TRACE_LEVEL_CORE);
}
