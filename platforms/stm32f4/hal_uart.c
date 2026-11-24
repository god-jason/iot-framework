/*
 * hal_uart.c - STM32F4 UART HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 *
 * 编号：config.id = 0..N 依次对应 USART1/USART2/USART3/UART4/UART5/
 * USART6；hal_uart_init() 内部按编号取静态 Cube 句柄，调用者无需提供实例。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

#define STM32_UART_COUNT   6U

static UART_HandleTypeDef s_huart[STM32_UART_COUNT];

/*
 * 每路 UART 的发送占用标志。Cube 的 HAL_UART_Transmit 是轮询实现，
 * 内部使用句柄上的 pTxBuffPtr/TxXferCount：若任务与中断（或两个任务）
 * 同时发送，这两组字段会被互相覆盖，导致从错误地址继续发送（会把
 * 栈内容当作数据发出去）。这里用标志拒绝并发发送，返回 HAL_BUSY。
 */
static volatile uint8_t s_tx_busy[STM32_UART_COUNT];

/*
 * 接收环形缓冲：UART 只有 1 字节硬件缓冲，任务内轮询读取时一旦被
 * 更高优先级任务抢占（如 shell 回显），成串输入就会溢出丢失。
 * 这里用 1 字节中断接收把数据先收进环形缓冲，任务再按需取出。
 */
#define STM32_UART_RX_BUF   128U

typedef struct {
    uint8_t           buf[STM32_UART_RX_BUF];
    volatile uint16_t head;     /* 写入位置（中断上下文） */
    volatile uint16_t tail;     /* 读取位置（任务上下文） */
} stm32_uart_rx_t;

static stm32_uart_rx_t s_rx[STM32_UART_COUNT];
static uint8_t         s_rx_byte[STM32_UART_COUNT];

/* 句柄 -> 通道编号（-1 表示不是本文件初始化的实例） */
static int stm32_uart_index(const UART_HandleTypeDef *huart)
{
    uint8_t i;

    for (i = 0U; i < STM32_UART_COUNT; i++) {
        if (huart == &s_huart[i]) {
            return (int)i;
        }
    }
    return -1;
}

/* 重新挂起 1 字节中断接收（在中断回调中调用） */
static void stm32_uart_rx_arm(UART_HandleTypeDef *huart, uint8_t id)
{
    (void)HAL_UART_Receive_IT(huart, &s_rx_byte[id], 1U);
}

static IRQn_Type stm32_uart_irqn(uint8_t id)
{
    switch (id) {
    case 0U: return USART1_IRQn;
    case 1U: return USART2_IRQn;
    case 2U: return USART3_IRQn;
    case 3U: return UART4_IRQn;
    case 4U: return UART5_IRQn;
    default: return USART6_IRQn;
    }
}

static void stm32_uart_irq_enable(uint8_t id)
{
    IRQn_Type irqn = stm32_uart_irqn(id);

    HAL_NVIC_SetPriority(irqn, 5U, 0U);
    HAL_NVIC_EnableIRQ(irqn);
}

/* 任务上下文取一个已接收字节；返回 false 表示缓冲为空 */
static bool stm32_uart_rx_pop(uint8_t id, uint8_t *out)
{
    stm32_uart_rx_t *r = &s_rx[id];
    uint16_t tail = r->tail;

    if (tail == r->head) {
        return false;
    }
    *out = r->buf[tail];
    r->tail = (uint16_t)((tail + 1U) % STM32_UART_RX_BUF);
    return true;
}

static USART_TypeDef *const s_uart_inst[STM32_UART_COUNT] = {
    [0] = USART1,
#ifdef USART2
    [1] = USART2,
#endif
#ifdef USART3
    [2] = USART3,
#endif
#ifdef UART4
    [3] = UART4,
#endif
#ifdef UART5
    [4] = UART5,
#endif
#ifdef USART6
    [5] = USART6,
#endif
};

static UART_HandleTypeDef *stm32_uart_instance(uint8_t instance)
{
    UART_HandleTypeDef *huart;

    if ((instance >= STM32_UART_COUNT) || (s_uart_inst[instance] == NULL)) {
        return NULL;
    }
    huart = &s_huart[instance];
    memset(huart, 0, sizeof(*huart));
    huart->Instance = s_uart_inst[instance];
    return huart;
}

static uint32_t stm32_uart_mode(hal_uart_mode_e mode)
{
    switch (mode) {
    case HAL_UART_MODE_TX:
        return UART_MODE_TX;
    case HAL_UART_MODE_RX:
        return UART_MODE_RX;
    case HAL_UART_MODE_TX_RX:
    default:
        return UART_MODE_TX_RX;
    }
}

int hal_uart_init(hal_uart_t *handle,
                             const hal_uart_config_t *config)
{
    UART_HandleTypeDef *huart;

    if ((handle == NULL) || (config == NULL) || (config->baudrate == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    huart = stm32_uart_instance(config->id);
    if (huart == NULL) {
        return HAL_INVALID_PARAM;
    }
    handle->base.instance = huart;

    if (config->word_length == HAL_UART_WORD_7BIT) {
        /* STM32F4 只支持 8/9 位数据。 */
        return HAL_NOT_SUPPORTED;
    }

    memset(&huart->Init, 0, sizeof(huart->Init));
    huart->Init.BaudRate = config->baudrate;
    huart->Init.WordLength = (config->word_length == HAL_UART_WORD_9BIT) ?
                             UART_WORDLENGTH_9B : UART_WORDLENGTH_8B;
    huart->Init.StopBits = (config->stop_bits == HAL_UART_STOP_2) ?
                           UART_STOPBITS_2 : UART_STOPBITS_1;
    switch (config->parity) {
    case HAL_UART_PARITY_EVEN:
        huart->Init.Parity = UART_PARITY_EVEN;
        break;
    case HAL_UART_PARITY_ODD:
        huart->Init.Parity = UART_PARITY_ODD;
        break;
    case HAL_UART_PARITY_NONE:
    default:
        huart->Init.Parity = UART_PARITY_NONE;
        break;
    }
    switch (config->flow_control) {
    case HAL_UART_FLOW_RTS:
        huart->Init.HwFlowCtl = UART_HWCONTROL_RTS;
        break;
    case HAL_UART_FLOW_CTS:
        huart->Init.HwFlowCtl = UART_HWCONTROL_CTS;
        break;
    case HAL_UART_FLOW_RTS_CTS:
        huart->Init.HwFlowCtl = UART_HWCONTROL_RTS_CTS;
        break;
    case HAL_UART_FLOW_NONE:
    default:
        huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
        break;
    }
    huart->Init.Mode = stm32_uart_mode(config->mode);
    huart->Init.OverSampling = config->oversampling ?
                               UART_OVERSAMPLING_16 : UART_OVERSAMPLING_8;

    if (HAL_UART_Init(huart) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;

    /* 启动 1 字节中断接收：清空缓冲与溢出标志后开中断 */
    memset(&s_rx[config->id], 0, sizeof(s_rx[config->id]));
    __HAL_UART_CLEAR_OREFLAG(huart);
    stm32_uart_rx_arm(huart, config->id);
    stm32_uart_irq_enable(config->id);
    return HAL_OK;
}

int hal_uart_deinit(hal_uart_t *handle)
{
    UART_HandleTypeDef *huart;
    int idx;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }

    huart = (UART_HandleTypeDef *)handle->base.instance;
    if (huart != NULL) {
        idx = stm32_uart_index(huart);
        if (idx >= 0) {
            HAL_NVIC_DisableIRQ(stm32_uart_irqn((uint8_t)idx));
            (void)HAL_UART_AbortReceive(huart);
            s_rx[idx].head = 0U;
            s_rx[idx].tail = 0U;
        }
        (void)HAL_UART_DeInit(huart);
    }

    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_uart_transmit(hal_uart_t *handle, const uint8_t *data,
                                 size_t length, uint32_t timeout)
{
    UART_HandleTypeDef *huart;
    HAL_StatusTypeDef status;
    uint8_t id;
    uint32_t primask;

    if ((handle == NULL) || (data == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_OK;
    }
    if (length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }

    huart = (UART_HandleTypeDef *)handle->base.instance;
    if (huart == NULL) {
        return HAL_NOT_READY;
    }

    /* 拒绝并发发送（仅临界区内做测试并置位，发送过程不关中断） */
    id = handle->config.id;
    if (id >= STM32_UART_COUNT) {
        return HAL_INVALID_PARAM;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    if (s_tx_busy[id] != 0U) {
        if (primask == 0U) {
            __enable_irq();
        }
        return HAL_BUSY;
    }
    s_tx_busy[id] = 1U;
    if (primask == 0U) {
        __enable_irq();
    }

    handle->base.state = HAL_STATE_BUSY_TX;
    status = HAL_UART_Transmit(huart, (uint8_t *)data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;

    s_tx_busy[id] = 0U;
    return ros_cube_status(status);
}

int hal_uart_receive(hal_uart_t *handle, uint8_t *data,
                                size_t length, uint32_t timeout)
{
    UART_HandleTypeDef *huart;
    uint8_t id;
    uint32_t start;
    size_t got = 0U;

    if ((handle == NULL) || (data == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_OK;
    }
    if (length > 0xFFFFU) {
        return HAL_INVALID_PARAM;
    }

    huart = (UART_HandleTypeDef *)handle->base.instance;
    if (huart == NULL) {
        return HAL_NOT_READY;
    }
    id = handle->config.id;
    if (id >= STM32_UART_COUNT) {
        return HAL_INVALID_PARAM;
    }

    /* 从中断接收环形缓冲取数据；超时返回已收到的部分 */
    start = HAL_GetTick();
    while (got < length) {
        if (stm32_uart_rx_pop(id, &data[got])) {
            got++;
            continue;
        }
        if (timeout == 0U) {
            break;
        }
        if ((timeout != HAL_TIMEOUT_FOREVER) &&
            ((uint32_t)(HAL_GetTick() - start) >= timeout)) {
            break;
        }
    }
    return (got == length) ? HAL_OK : HAL_TIMEOUT;
}

int hal_uart_transmit_it(hal_uart_t *handle,
                                    const uint8_t *data, size_t length)
{
    UART_HandleTypeDef *huart;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }

    huart = (UART_HandleTypeDef *)handle->base.instance;
    if (huart == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_UART_Transmit_IT(huart, (uint8_t *)data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX;
    }
    return ros_cube_status(status);
}

int hal_uart_receive_it(hal_uart_t *handle, uint8_t *data,
                                   size_t length)
{
    UART_HandleTypeDef *huart;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }

    huart = (UART_HandleTypeDef *)handle->base.instance;
    if (huart == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_UART_Receive_IT(huart, data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_RX;
    }
    return ros_cube_status(status);
}

int hal_uart_transmit_dma(hal_uart_t *handle,
                                     const uint8_t *data, size_t length)
{
    UART_HandleTypeDef *huart;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }

    huart = (UART_HandleTypeDef *)handle->base.instance;
    if (huart == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_UART_Transmit_DMA(huart, (uint8_t *)data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX;
    }
    return ros_cube_status(status);
}

int hal_uart_receive_dma(hal_uart_t *handle, uint8_t *data,
                                    size_t length)
{
    UART_HandleTypeDef *huart;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }

    huart = (UART_HandleTypeDef *)handle->base.instance;
    if (huart == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_UART_Receive_DMA(huart, data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_RX;
    }
    return ros_cube_status(status);
}

int hal_uart_receive_to_idle(hal_uart_t *handle,
                                        uint8_t *data, size_t length,
                                        hal_transfer_mode_e mode)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    HAL_UNUSED(mode);
    /* STM32F4 的 Cube HAL 不提供 HAL_UARTEx_ReceiveToIdle_DMA。 */
    return HAL_NOT_SUPPORTED;
}

size_t hal_uart_get_rx_count(hal_uart_t *handle)
{
    UART_HandleTypeDef *huart;

    if ((handle == NULL) || (handle->base.instance == NULL)) {
        return 0U;
    }
    huart = (UART_HandleTypeDef *)handle->base.instance;
    if (huart->RxState == HAL_UART_STATE_BUSY_RX) {
        return (size_t)(huart->RxXferSize - huart->RxXferCount);
    }
    return 0U;
}

int hal_uart_abort_tx(hal_uart_t *handle)
{
    UART_HandleTypeDef *huart;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    huart = (UART_HandleTypeDef *)handle->base.instance;
    if (huart == NULL) {
        return HAL_NOT_READY;
    }
    handle->base.state = HAL_STATE_READY;
    return ros_cube_status(HAL_UART_AbortTransmit(huart));
}

int hal_uart_abort_rx(hal_uart_t *handle)
{
    UART_HandleTypeDef *huart;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    huart = (UART_HandleTypeDef *)handle->base.instance;
    if (huart == NULL) {
        return HAL_NOT_READY;
    }
    handle->base.state = HAL_STATE_READY;
    return ros_cube_status(HAL_UART_AbortReceive(huart));
}

int hal_uart_abort(hal_uart_t *handle)
{
    UART_HandleTypeDef *huart;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    huart = (UART_HandleTypeDef *)handle->base.instance;
    if (huart == NULL) {
        return HAL_NOT_READY;
    }
    handle->base.state = HAL_STATE_READY;
    return ros_cube_status(HAL_UART_Abort(huart));
}

hal_state_e hal_uart_get_state(hal_uart_t *handle)
{
    return (handle != NULL) ? handle->base.state : HAL_STATE_RESET;
}

int hal_uart_get_error(hal_uart_t *handle)
{
    return (handle != NULL) ? handle->base.error : HAL_INVALID_PARAM;
}

int hal_uart_register_callback(hal_uart_t *handle,
                                          hal_uart_callback_t callback,
                                          void *user_data)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    handle->callback = callback;
    handle->user_data = user_data;
    return HAL_OK;
}

/*
 * 供 USARTx_IRQHandler() 调用。用户工程的中断服务函数应替换为：
 *   void USART1_IRQHandler(void) { ros_stm32_uart_irq_handler(&huart1_port); }
 * 本函数内部调用 HAL_UART_IRQHandler()，然后按状态派发 PORT 回调。
 */
void ros_stm32_uart_irq_handler(hal_uart_t *handle)
{
    UART_HandleTypeDef *huart;
    hal_state_e previous;

    if ((handle == NULL) || (handle->base.instance == NULL)) {
        return;
    }

    huart = (UART_HandleTypeDef *)handle->base.instance;
    previous = handle->base.state;

    HAL_UART_IRQHandler(huart);

    if ((previous == HAL_STATE_BUSY_TX) &&
        (huart->gState == HAL_UART_STATE_READY)) {
        handle->base.state = HAL_STATE_READY;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_UART_EVENT_TX_COMPLETE,
                             (size_t)huart->TxXferSize, handle->user_data);
        }
    }

    if ((previous == HAL_STATE_BUSY_RX) &&
        (huart->RxState == HAL_UART_STATE_READY)) {
        handle->base.state = HAL_STATE_READY;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_UART_EVENT_RX_COMPLETE,
                             (size_t)huart->RxXferSize, handle->user_data);
        }
    }

    if (huart->ErrorCode != HAL_UART_ERROR_NONE) {
        handle->base.error = HAL_ERROR;
        handle->base.state = HAL_STATE_ERROR;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_UART_EVENT_ERROR, 0U,
                             handle->user_data);
        }
        huart->ErrorCode = HAL_UART_ERROR_NONE;
    }
}

/* ============================== 中断接收 ============================== */

/* Cube HAL 的接收完成回调：把字节压入环形缓冲并重新挂起接收 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    stm32_uart_rx_t *r;
    uint16_t next;
    int idx = stm32_uart_index(huart);

    if (idx < 0) {
        return;
    }
    r = &s_rx[idx];
    next = (uint16_t)((r->head + 1U) % STM32_UART_RX_BUF);
    if (next != r->tail) {
        r->buf[r->head] = s_rx_byte[idx];
        r->head = next;
    }
    stm32_uart_rx_arm(huart, (uint8_t)idx);
}

/* 溢出/帧错误等：清标志后重新挂起接收，避免停摆 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    int idx = stm32_uart_index(huart);

    if (idx < 0) {
        return;
    }
    (void)HAL_UART_AbortReceive(huart);
    __HAL_UART_CLEAR_OREFLAG(huart);
    stm32_uart_rx_arm(huart, (uint8_t)idx);
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_huart[0]);
}

#ifdef USART2
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_huart[1]);
}
#endif

#ifdef USART3
void USART3_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_huart[2]);
}
#endif

#ifdef UART4
void UART4_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_huart[3]);
}
#endif

#ifdef UART5
void UART5_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_huart[4]);
}
#endif

#ifdef USART6
void USART6_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_huart[5]);
}
#endif


#endif /* ROS_STM32_F4 */
