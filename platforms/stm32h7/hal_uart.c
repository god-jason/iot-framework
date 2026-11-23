/*
 * hal_uart.c - STM32H7 UART HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 *
 * 编号：config.id = 0..N 依次对应 USART1/USART2/USART3/UART4/UART5/
 * USART6；hal_uart_init() 内部按编号取静态 Cube 句柄，调用者无需提供实例。
 */

#if defined(ROS_STM32_H7)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

#define STM32_UART_COUNT   6U

static UART_HandleTypeDef s_huart[STM32_UART_COUNT];

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
        /* STM32H7 只支持 8/9 位数据。 */
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
    return HAL_OK;
}

int hal_uart_deinit(hal_uart_t *handle)
{
    UART_HandleTypeDef *huart;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }

    huart = (UART_HandleTypeDef *)handle->base.instance;
    if (huart != NULL) {
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

    handle->base.state = HAL_STATE_BUSY_TX;
    status = HAL_UART_Transmit(huart, (uint8_t *)data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return ros_cube_status(status);
}

int hal_uart_receive(hal_uart_t *handle, uint8_t *data,
                                size_t length, uint32_t timeout)
{
    UART_HandleTypeDef *huart;
    HAL_StatusTypeDef status;

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

    handle->base.state = HAL_STATE_BUSY_RX;
    status = HAL_UART_Receive(huart, data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return ros_cube_status(status);
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
    /* STM32H7 的 Cube HAL 不提供 HAL_UARTEx_ReceiveToIdle_DMA。 */
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


#endif /* ROS_STM32_H7 */
