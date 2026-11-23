/*
 * hal_uart.c - STM32F1 USART/UART 寄存器实现（不依赖 Cube HAL）
 *
 * 按器件头提供的实例自动编入 USART1~USART3、UART4~UART5（F103 中容量
 * 仅 USART1~3；大容量才有 UART4/5）。
 *
 *   引脚（默认复用，未做重映射）：
 *     USART1 PA9/PA10   USART2 PA2/PA3    USART3 PB10/PB11
 *     UART4  PA0/PA1    UART5  PC12/PD2
 *
 * 接收使用中断 + 每路独立环形缓冲；阻塞发送、阻塞/非阻塞接收。
 * 帧格式支持 8/9 位数据、奇偶校验、1/2 停止位；硬件流控与 DMA 未实现。
 *
 * 句柄约定：config.id 为 0 基编号，依次对应
 *   USART1 / USART2 / USART3 / UART4 / UART5（按芯片实际可用实例裁剪）；
 *   hal_uart_init() 内部按编号解析寄存器实例，调用者无需指定 base.instance。
 */

#if defined(ROS_STM32_F1)

#include "hal.h"
#include "common.h"
#include "stm32f10x.h"

#include <stddef.h>

/*
 * 器件头里 UART4/UART5 的寄存器宏是无条件定义的，但 IRQn 只在大容量/
 * 互联型中提供；这里按密度宏判断实例是否真实可用。
 */
#if defined(STM32F10X_MD) || defined(STM32F10X_MD_VL) || \
    defined(STM32F10X_HD) || defined(STM32F10X_HD_VL) || \
    defined(STM32F10X_XL) || defined(STM32F10X_CL)
#define STM32_UART_HAS_USART3   1
#endif

#if defined(STM32F10X_HD) || defined(STM32F10X_HD_VL) || \
    defined(STM32F10X_XL) || defined(STM32F10X_CL)
#define STM32_UART_HAS_UART45   1
#endif

/* USART1/2 承担日志与模组通信，缓冲区大一些；其余通道按需取小缓冲。 */
#define STM32_UART_RX_BIG     128U
#define STM32_UART_RX_SMALL   64U

#define STM32_USART_SR_PE      (1UL << 0)
#define STM32_USART_SR_FE      (1UL << 1)
#define STM32_USART_SR_NE      (1UL << 2)
#define STM32_USART_SR_ORE     (1UL << 3)
#define STM32_USART_SR_RXNE    (1UL << 5)
#define STM32_USART_SR_TXE     (1UL << 7)

#define STM32_USART_CR1_RE     (1UL << 2)
#define STM32_USART_CR1_TE     (1UL << 3)
#define STM32_USART_CR1_RXNEIE (1UL << 5)
#define STM32_USART_CR1_PS     (1UL << 9)
#define STM32_USART_CR1_PCE    (1UL << 10)
#define STM32_USART_CR1_M      (1UL << 12)
#define STM32_USART_CR1_UE     (1UL << 13)
#define STM32_USART_CR1_OVER8  (1UL << 15)

typedef struct {
    hal_uart_t *handle;
    ringbuf_t rx;
    bool ready;
} stm32_uart_ch_t;

static uint8_t s_rx_usart1[STM32_UART_RX_BIG];
#ifdef USART2
static uint8_t s_rx_usart2[STM32_UART_RX_BIG];
#endif
#if STM32_UART_HAS_USART3
static uint8_t s_rx_usart3[STM32_UART_RX_SMALL];
#endif
#if STM32_UART_HAS_UART45
static uint8_t s_rx_uart4[STM32_UART_RX_SMALL];
#endif
#if STM32_UART_HAS_UART45
static uint8_t s_rx_uart5[STM32_UART_RX_SMALL];
#endif

static USART_TypeDef *const s_uart_inst[] = {
    USART1,
#ifdef USART2
    USART2,
#endif
#if STM32_UART_HAS_USART3
    USART3,
#endif
#if STM32_UART_HAS_UART45
    UART4,
#endif
#if STM32_UART_HAS_UART45
    UART5,
#endif
};

#define STM32_UART_CH_COUNT \
    (sizeof(s_uart_inst) / sizeof(s_uart_inst[0]))

static stm32_uart_ch_t s_uart_ch[STM32_UART_CH_COUNT];

static uint8_t stm32_uart_index(USART_TypeDef *u)
{
    uint8_t i;

    for (i = 0U; i < STM32_UART_CH_COUNT; i++) {
        if (s_uart_inst[i] == u) {
            return i;
        }
    }
    return 0xFFU;
}

static uint8_t *stm32_uart_rx_mem(USART_TypeDef *u)
{
    if (u == USART1) {
        return s_rx_usart1;
    }
#ifdef USART2
    if (u == USART2) {
        return s_rx_usart2;
    }
#endif
#if STM32_UART_HAS_USART3
    if (u == USART3) {
        return s_rx_usart3;
    }
#endif
#if STM32_UART_HAS_UART45
    if (u == UART4) {
        return s_rx_uart4;
    }
#endif
#if STM32_UART_HAS_UART45
    if (u == UART5) {
        return s_rx_uart5;
    }
#endif
    return NULL;
}

static size_t stm32_uart_rx_cap(USART_TypeDef *u)
{
    if (u == USART1) {
        return STM32_UART_RX_BIG;
    }
#ifdef USART2
    if (u == USART2) {
        return STM32_UART_RX_BIG;
    }
#endif
    return STM32_UART_RX_SMALL;
}

static IRQn_Type stm32_uart_irqn(USART_TypeDef *u)
{
    if (u == USART1) {
        return USART1_IRQn;
    }
#ifdef USART2
    if (u == USART2) {
        return USART2_IRQn;
    }
#endif
#if STM32_UART_HAS_USART3
    if (u == USART3) {
        return USART3_IRQn;
    }
#endif
#if STM32_UART_HAS_UART45
    if (u == UART4) {
        return UART4_IRQn;
    }
#endif
#if STM32_UART_HAS_UART45
    if (u == UART5) {
        return UART5_IRQn;
    }
#endif
    return USART1_IRQn;
}

static uint32_t stm32_pclk1(void)
{
    uint32_t pre = (RCC->CFGR >> 8) & 0x7U;

    switch (pre) {
    case 4U: return SystemCoreClock / 2U;
    case 5U: return SystemCoreClock / 4U;
    case 6U: return SystemCoreClock / 8U;
    case 7U: return SystemCoreClock / 16U;
    default: return SystemCoreClock;
    }
}

static uint32_t stm32_pclk2(void)
{
    uint32_t pre = (RCC->CFGR >> 11) & 0x7U;

    switch (pre) {
    case 4U: return SystemCoreClock / 2U;
    case 5U: return SystemCoreClock / 4U;
    case 6U: return SystemCoreClock / 8U;
    case 7U: return SystemCoreClock / 16U;
    default: return SystemCoreClock;
    }
}

static uint32_t stm32_uart_brr(uint32_t pclk, uint32_t baud, bool over8)
{
    uint32_t scaled = (pclk + (baud / 2U)) / baud;

    if (!over8) {
        return scaled & 0xFFFFU;
    }
    /* OVER8：小数位为 3 位 */
    return ((scaled / 8U) << 4) | (scaled & 0x7U);
}

static void stm32_uart_pins_init(USART_TypeDef *u)
{
    if (u == USART1) {
        RCC->APB2ENR |= (1UL << 2);         /* GPIOA */
        /* PA9 AF 推挽 50MHz，PA10 上拉输入 */
        GPIOA->CRH = (GPIOA->CRH & ~(0xFFUL << 4)) | (0x0BUL << 4) |
                     (0x08UL << 8);
        GPIOA->ODR |= (1UL << 10);
    }
#ifdef USART2
    else if (u == USART2) {
        RCC->APB2ENR |= (1UL << 2);         /* GPIOA */
        /* PA2 AF 推挽 50MHz，PA3 上拉输入 */
        GPIOA->CRL = (GPIOA->CRL & ~(0xFFUL << 8)) | (0x0BUL << 8) |
                     (0x08UL << 12);
        GPIOA->ODR |= (1UL << 3);
    }
#endif
#if STM32_UART_HAS_USART3
    else if (u == USART3) {
        RCC->APB2ENR |= (1UL << 3);         /* GPIOB */
        /* PB10 AF 推挽 50MHz，PB11 上拉输入 */
        GPIOB->CRH = (GPIOB->CRH & ~(0xFFUL << 8)) | (0x0BUL << 8) |
                     (0x08UL << 12);
        GPIOB->ODR |= (1UL << 11);
    }
#endif
#if STM32_UART_HAS_UART45
    else if (u == UART4) {
        RCC->APB2ENR |= (1UL << 2);         /* GPIOA */
        /* PA0 AF 推挽 50MHz，PA1 上拉输入（与 TIM2_CH1/CH2 冲突） */
        GPIOA->CRL = (GPIOA->CRL & ~(0xFFUL << 0)) | (0x0BUL << 0) |
                     (0x08UL << 4);
        GPIOA->ODR |= (1UL << 1);
    }
#endif
#if STM32_UART_HAS_UART45
    else if (u == UART5) {
        RCC->APB2ENR |= (1UL << 4) | (1UL << 5);   /* GPIOC | GPIOD */
        /* PC12 AF 推挽 50MHz，PD2 上拉输入 */
        GPIOC->CRH = (GPIOC->CRH & ~(0x0FUL << 16)) | (0x0BUL << 16);
        GPIOD->CRL = (GPIOD->CRL & ~(0x0FUL << 8)) | (0x08UL << 8);
        GPIOD->ODR |= (1UL << 2);
    }
#endif
}

static void stm32_uart_clock_enable(USART_TypeDef *u)
{
    if (u == USART1) {
        RCC->APB2ENR |= (1UL << 14);
    }
#ifdef USART2
    else if (u == USART2) {
        RCC->APB1ENR |= (1UL << 17);
    }
#endif
#if STM32_UART_HAS_USART3
    else if (u == USART3) {
        RCC->APB1ENR |= (1UL << 18);
    }
#endif
#if STM32_UART_HAS_UART45
    else if (u == UART4) {
        RCC->APB1ENR |= (1UL << 19);
    }
#endif
#if STM32_UART_HAS_UART45
    else if (u == UART5) {
        RCC->APB1ENR |= (1UL << 20);
    }
#endif
}

int hal_uart_init(hal_uart_t *handle, const hal_uart_config_t *config)
{
    USART_TypeDef *u;
    stm32_uart_ch_t *ch;
    uint8_t *rx_mem;
    uint32_t cr1 = 0U;
    uint32_t pclk;
    uint8_t index;
    bool over8;

    if ((handle == NULL) || (config == NULL) || (config->baudrate == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if (config->flow_control != HAL_UART_FLOW_NONE) {
        return HAL_NOT_SUPPORTED;
    }
    if ((config->word_length != HAL_UART_WORD_8BIT) &&
        (config->word_length != HAL_UART_WORD_9BIT)) {
        return HAL_NOT_SUPPORTED;
    }
    /* OVER8 不能与 9 位数据同时使用 */
    if ((config->word_length == HAL_UART_WORD_9BIT) &&
        config->oversampling) {
        return HAL_NOT_SUPPORTED;
    }

    if (config->id >= STM32_UART_CH_COUNT) {
        return HAL_INVALID_PARAM;
    }
    index = config->id;
    u = s_uart_inst[index];
    rx_mem = stm32_uart_rx_mem(u);
    if (rx_mem == NULL) {
        return HAL_INVALID_PARAM;
    }
    ch = &s_uart_ch[index];
    handle->base.instance = u;
    stm32_uart_clock_enable(u);
    stm32_uart_pins_init(u);

    u->CR1 = 0U;
    u->CR2 = 0U;
    u->CR3 = 0U;

    over8 = config->oversampling;
    pclk = (u == USART1) ? stm32_pclk2() : stm32_pclk1();
    u->BRR = stm32_uart_brr(pclk, config->baudrate, over8);

    cr1 = STM32_USART_CR1_UE | STM32_USART_CR1_RXNEIE;
    if (config->mode & HAL_UART_MODE_TX) {
        cr1 |= STM32_USART_CR1_TE;
    }
    if (config->mode & HAL_UART_MODE_RX) {
        cr1 |= STM32_USART_CR1_RE;
    }
    if (config->word_length == HAL_UART_WORD_9BIT) {
        cr1 |= STM32_USART_CR1_M;
    }
    if (config->parity != HAL_UART_PARITY_NONE) {
        cr1 |= STM32_USART_CR1_PCE;
        if (config->parity == HAL_UART_PARITY_ODD) {
            cr1 |= STM32_USART_CR1_PS;
        }
    }
    if (over8) {
        cr1 |= STM32_USART_CR1_OVER8;
    }

    if (config->stop_bits == HAL_UART_STOP_2) {
        u->CR2 = (2UL << 12);
    }

    (void)ringbuf_init(&ch->rx, rx_mem, stm32_uart_rx_cap(u));
    ch->handle = handle;
    ch->ready = true;

    u->CR1 = cr1;

    NVIC_SetPriority(stm32_uart_irqn(u), 6U);
    NVIC_EnableIRQ(stm32_uart_irqn(u));

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_uart_deinit(hal_uart_t *handle)
{
    USART_TypeDef *u;
    uint8_t index;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    u = (USART_TypeDef *)handle->base.instance;
    index = stm32_uart_index(u);
    if (index < STM32_UART_CH_COUNT) {
        u->CR1 = 0U;
        NVIC_DisableIRQ(stm32_uart_irqn(u));
        s_uart_ch[index].ready = false;
        s_uart_ch[index].handle = NULL;
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

static int stm32_uart_wait_txe(USART_TypeDef *u, uint32_t timeout)
{
    uint32_t start = hal_tick_get();

    while ((u->SR & STM32_USART_SR_TXE) == 0U) {
        if (timeout == HAL_TIMEOUT_NONE) {
            return HAL_TIMEOUT;
        }
        if (timeout != HAL_TIMEOUT_FOREVER &&
            ((uint32_t)(hal_tick_get() - start) >= timeout)) {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

int hal_uart_transmit(hal_uart_t *handle, const uint8_t *data,
                      size_t length, uint32_t timeout)
{
    USART_TypeDef *u;
    size_t i;

    if ((handle == NULL) || (data == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (length == 0U) {
        return HAL_OK;
    }
    if (handle->base.state != HAL_STATE_READY &&
        handle->base.state != HAL_STATE_BUSY_TX) {
        return HAL_NOT_READY;
    }

    u = (USART_TypeDef *)handle->base.instance;
    handle->base.state = HAL_STATE_BUSY_TX;
    for (i = 0U; i < length; i++) {
        if (stm32_uart_wait_txe(u, timeout) != HAL_OK) {
            handle->base.state = HAL_STATE_ERROR;
            return HAL_TIMEOUT;
        }
        u->DR = data[i];
    }
    /* 等待最后一个字节移出，保证返回后发送完成（有界等待防死锁） */
    {
        uint32_t spin = 2000000U;

        while ((u->SR & STM32_USART_SR_TXE) == 0U) {
            if (--spin == 0U) {
                handle->base.state = HAL_STATE_ERROR;
                return HAL_TIMEOUT;
            }
        }
    }
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_uart_receive(hal_uart_t *handle, uint8_t *data,
                     size_t length, uint32_t timeout)
{
    USART_TypeDef *u;
    stm32_uart_ch_t *ch;
    uint8_t index;
    uint32_t start;
    size_t got = 0U;

    if ((handle == NULL) || (data == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (length == 0U) {
        return HAL_OK;
    }
    u = (USART_TypeDef *)handle->base.instance;
    index = stm32_uart_index(u);
    if ((index >= STM32_UART_CH_COUNT) || !s_uart_ch[index].ready) {
        return HAL_NOT_READY;
    }
    ch = &s_uart_ch[index];

    start = hal_tick_get();
    while (got < length) {
        if (ringbuf_read(&ch->rx, &data[got], 1U) == 1U) {
            got++;
            continue;
        }
        if (timeout == HAL_TIMEOUT_NONE) {
            break;
        }
        if (timeout != HAL_TIMEOUT_FOREVER &&
            ((uint32_t)(hal_tick_get() - start) >= timeout)) {
            break;
        }
    }
    return (got == length) ? HAL_OK : HAL_TIMEOUT;
}

size_t hal_uart_get_rx_count(hal_uart_t *handle)
{
    USART_TypeDef *u;
    uint8_t index;

    if (handle == NULL) {
        return 0U;
    }
    u = (USART_TypeDef *)handle->base.instance;
    index = stm32_uart_index(u);
    if ((index >= STM32_UART_CH_COUNT) || !s_uart_ch[index].ready) {
        return 0U;
    }
    return (size_t)ringbuf_available(&s_uart_ch[index].rx);
}

int hal_uart_transmit_it(hal_uart_t *handle, const uint8_t *data, size_t length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    return HAL_NOT_SUPPORTED;
}

int hal_uart_receive_it(hal_uart_t *handle, uint8_t *data, size_t length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    return HAL_NOT_SUPPORTED;
}

int hal_uart_transmit_dma(hal_uart_t *handle, const uint8_t *data, size_t length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    return HAL_NOT_SUPPORTED;
}

int hal_uart_receive_dma(hal_uart_t *handle, uint8_t *data, size_t length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    return HAL_NOT_SUPPORTED;
}

int hal_uart_receive_to_idle(hal_uart_t *handle, uint8_t *data,
                             size_t length, hal_transfer_mode_e mode)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    HAL_UNUSED(mode);
    return HAL_NOT_SUPPORTED;
}

int hal_uart_abort_tx(hal_uart_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_BUSY_TX) {
        handle->base.state = HAL_STATE_READY;
    }
    return HAL_OK;
}

int hal_uart_abort_rx(hal_uart_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    return HAL_OK;
}

int hal_uart_abort(hal_uart_t *handle)
{
    int result = hal_uart_abort_tx(handle);

    if (result != HAL_OK) {
        return result;
    }
    return hal_uart_abort_rx(handle);
}

hal_state_e hal_uart_get_state(hal_uart_t *handle)
{
    if (handle == NULL) {
        return HAL_STATE_RESET;
    }
    return handle->base.state;
}

int hal_uart_get_error(hal_uart_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    return handle->base.error;
}

int hal_uart_register_callback(hal_uart_t *handle,
                               hal_uart_callback_t callback, void *user_data)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    handle->callback = callback;
    handle->user_data = user_data;
    return HAL_OK;
}

static void stm32_uart_irq(USART_TypeDef *u)
{
    uint32_t sr = u->SR;
    uint8_t index = stm32_uart_index(u);

    if ((sr & (STM32_USART_SR_RXNE | STM32_USART_SR_ORE |
               STM32_USART_SR_FE | STM32_USART_SR_NE |
               STM32_USART_SR_PE)) == 0U) {
        return;
    }

    {
        uint8_t ch = (uint8_t)(u->DR & 0xFFU);

        if ((index < STM32_UART_CH_COUNT) && s_uart_ch[index].ready &&
            ((sr & STM32_USART_SR_RXNE) != 0U)) {
            (void)ringbuf_write(&s_uart_ch[index].rx, &ch, 1U);
        }
    }

    if ((index < STM32_UART_CH_COUNT) &&
        (sr & (STM32_USART_SR_ORE | STM32_USART_SR_FE |
               STM32_USART_SR_NE | STM32_USART_SR_PE)) != 0U) {
        hal_uart_t *handle = s_uart_ch[index].handle;

        if ((handle != NULL) && (handle->callback != NULL)) {
            handle->callback(handle, HAL_UART_EVENT_ERROR, 0U,
                             handle->user_data);
        }
    }
}

void USART1_IRQHandler(void)
{
    stm32_uart_irq(USART1);
}

#ifdef USART2
void USART2_IRQHandler(void)
{
    stm32_uart_irq(USART2);
}
#endif

#if STM32_UART_HAS_USART3
void USART3_IRQHandler(void)
{
    stm32_uart_irq(USART3);
}
#endif

#if STM32_UART_HAS_UART45
void UART4_IRQHandler(void)
{
    stm32_uart_irq(UART4);
}
#endif

#if STM32_UART_HAS_UART45
void UART5_IRQHandler(void)
{
    stm32_uart_irq(UART5);
}
#endif

#endif /* ROS_STM32_F1 */
