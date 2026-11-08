#if defined(IOT_STM32_F4)

/* STM32F4 平台实现：新 PORT 公共 API -> STM32CubeF4 HAL。
 *
 * 依赖约定：
 *   - 外设 handle->base.instance 指向用户工程中已存在的 HAL 句柄
 *     （UART_HandleTypeDef / SPI_HandleTypeDef / ...）。
 *   - hal_capture_t 的通道固定为 1
 *     （1..4，默认 1）。
 *   - 本文件只做外设驱动适配，系统时钟树建议仍由 CubeMX 生成的
 *     SystemClock_Config() 完成；hal_clock_init() 仅提供基础配置能力。
 *
 * 注意：本文件依赖 STM32CubeF4 HAL，仅在 STM32 交叉编译目标中参与构建。
 */
#include "hal.h"
#include "stm32.h"
#include "stm32f4xx_hal.h"

#include <string.h>

/* ========================================================================== */
/* 通用工具                                                                   */
/* ========================================================================== */

static hal_status_t f4_hal_status(HAL_StatusTypeDef status)
{
    switch (status) {
    case HAL_OK:
        return HAL_OK;
    case HAL_BUSY:
        return HAL_BUSY;
    case HAL_TIMEOUT:
        return HAL_TIMEOUT;
    case HAL_ERROR:
    default:
        return HAL_ERROR;
    }
}

/* ========================================================================== */
/* GPIO / PIN                                                                 */
/* ========================================================================== */

static GPIO_TypeDef *f4_gpio_port(uint32_t index)
{
    switch (index) {
    case 0U:
        return GPIOA;
    case 1U:
        return GPIOB;
#ifdef GPIOC
    case 2U:
        return GPIOC;
#endif
#ifdef GPIOD
    case 3U:
        return GPIOD;
#endif
#ifdef GPIOE
    case 4U:
        return GPIOE;
#endif
#ifdef GPIOF
    case 5U:
        return GPIOF;
#endif
#ifdef GPIOG
    case 6U:
        return GPIOG;
#endif
#ifdef GPIOH
    case 7U:
        return GPIOH;
#endif
#ifdef GPIOI
    case 8U:
        return GPIOI;
#endif
#ifdef GPIOJ
    case 9U:
        return GPIOJ;
#endif
#ifdef GPIOK
    case 10U:
        return GPIOK;
#endif
    default:
        return NULL;
    }
}

static uint16_t f4_pin_mask(uint32_t index)
{
    return (uint16_t)(1UL << index);
}

static uint32_t f4_gpio_speed(hal_gpio_speed_t speed)
{
    switch (speed) {
    case HAL_GPIO_SPEED_LOW:
        return GPIO_SPEED_FREQ_LOW;
    case HAL_GPIO_SPEED_MEDIUM:
        return GPIO_SPEED_FREQ_MEDIUM;
    case HAL_GPIO_SPEED_HIGH:
        return GPIO_SPEED_FREQ_HIGH;
    case HAL_GPIO_SPEED_VERY_HIGH:
        return GPIO_SPEED_FREQ_VERY_HIGH;
    default:
        return GPIO_SPEED_FREQ_LOW;
    }
}

static uint32_t f4_gpio_pull(hal_gpio_pull_t pull)
{
    switch (pull) {
    case HAL_GPIO_PULLUP:
        return GPIO_PULLUP;
    case HAL_GPIO_PULLDOWN:
        return GPIO_PULLDOWN;
    case HAL_GPIO_NOPULL:
    default:
        return GPIO_NOPULL;
    }
}

hal_status_t hal_gpio_init(hal_pin_t pin, const hal_gpio_config_t *config)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef *port;
    uint32_t index;

    if (config == NULL) {
        return HAL_INVALID_PARAM;
    }

    port = f4_gpio_port(HAL_PIN_GET_PORT(pin));
    index = HAL_PIN_GET_INDEX(pin);
    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }

    memset(&init, 0, sizeof(init));
    init.Pin = f4_pin_mask(index);
    init.Pull = f4_gpio_pull(config->pull);
    init.Speed = f4_gpio_speed(config->speed);

    switch (config->mode) {
    case HAL_GPIO_MODE_INPUT:
        init.Mode = GPIO_MODE_INPUT;
        break;
    case HAL_GPIO_MODE_OUTPUT:
        init.Mode = (config->output == HAL_GPIO_OUTPUT_OD) ?
                    GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT_PP;
        HAL_GPIO_WritePin(port, init.Pin,
                          (config->initial_level == HAL_GPIO_HIGH) ?
                          GPIO_PIN_SET : GPIO_PIN_RESET);
        break;
    case HAL_GPIO_MODE_ANALOG:
        init.Mode = GPIO_MODE_ANALOG;
        init.Pull = GPIO_NOPULL;
        break;
    case HAL_GPIO_MODE_AF:
        init.Mode = (config->output == HAL_GPIO_OUTPUT_OD) ?
                    GPIO_MODE_AF_OD : GPIO_MODE_AF_PP;
        init.Alternate = config->alternate;
        break;
    default:
        return HAL_INVALID_PARAM;
    }

    HAL_GPIO_Init(port, &init);
    return HAL_OK;
}

hal_status_t hal_gpio_deinit(hal_pin_t pin)
{
    GPIO_TypeDef *port = f4_gpio_port(HAL_PIN_GET_PORT(pin));
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }
    HAL_GPIO_DeInit(port, f4_pin_mask(index));
    return HAL_OK;
}

hal_status_t hal_gpio_write(hal_pin_t pin, hal_gpio_level_t level)
{
    GPIO_TypeDef *port = f4_gpio_port(HAL_PIN_GET_PORT(pin));
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }
    HAL_GPIO_WritePin(port, f4_pin_mask(index),
                      (level == HAL_GPIO_HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return HAL_OK;
}

hal_status_t hal_gpio_read(hal_pin_t pin, hal_gpio_level_t *level)
{
    GPIO_TypeDef *port = f4_gpio_port(HAL_PIN_GET_PORT(pin));
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if ((port == NULL) || (index > 15U) || (level == NULL)) {
        return HAL_INVALID_PARAM;
    }
    *level = (HAL_GPIO_ReadPin(port, f4_pin_mask(index)) == GPIO_PIN_SET) ?
             HAL_GPIO_HIGH : HAL_GPIO_LOW;
    return HAL_OK;
}

hal_status_t hal_gpio_toggle(hal_pin_t pin)
{
    GPIO_TypeDef *port = f4_gpio_port(HAL_PIN_GET_PORT(pin));
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }
    HAL_GPIO_TogglePin(port, f4_pin_mask(index));
    return HAL_OK;
}

hal_status_t hal_gpio_set(hal_pin_t pin)
{
    return hal_gpio_write(pin, HAL_GPIO_HIGH);
}

hal_status_t hal_gpio_reset(hal_pin_t pin)
{
    return hal_gpio_write(pin, HAL_GPIO_LOW);
}

hal_status_t hal_pin_init(const hal_pin_config_t *config)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef *port;
    uint32_t index;

    if (config == NULL) {
        return HAL_INVALID_PARAM;
    }

    port = f4_gpio_port(HAL_PIN_GET_PORT(config->pin));
    index = HAL_PIN_GET_INDEX(config->pin);
    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }

    memset(&init, 0, sizeof(init));
    init.Pin = f4_pin_mask(index);
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;

    switch (config->function) {
    case HAL_PIN_FUNCTION_GPIO:
        init.Mode = GPIO_MODE_INPUT;
        break;
    case HAL_PIN_FUNCTION_AF:
        init.Mode = GPIO_MODE_AF_PP;
        init.Alternate = config->alternate;
        break;
    case HAL_PIN_FUNCTION_ANALOG:
        init.Mode = GPIO_MODE_ANALOG;
        break;
    default:
        return HAL_INVALID_PARAM;
    }

    HAL_GPIO_Init(port, &init);
    return HAL_OK;
}

hal_status_t hal_pin_deinit(hal_pin_t pin)
{
    return hal_gpio_deinit(pin);
}

/* ========================================================================== */
/* EXTI                                                                       */
/* ========================================================================== */

typedef struct {
    bool used;
    hal_pin_t pin;
    hal_exti_callback_t callback;
    void *user_data;
} f4_exti_slot_t;

static f4_exti_slot_t s_f4_exti[16];

static IRQn_Type f4_exti_irqn(uint32_t index)
{
    if (index <= 4U) {
        return (IRQn_Type)(EXTI0_IRQn + (int32_t)index);
    }
    if (index <= 9U) {
        return EXTI9_5_IRQn;
    }
    return EXTI15_10_IRQn;
}

hal_status_t hal_exti_init(hal_pin_t pin, const hal_exti_config_t *config)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef *port;
    IRQn_Type irqn;
    uint32_t index;

    if (config == NULL) {
        return HAL_INVALID_PARAM;
    }

    port = f4_gpio_port(HAL_PIN_GET_PORT(pin));
    index = HAL_PIN_GET_INDEX(pin);
    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }

    memset(&init, 0, sizeof(init));
    init.Pin = f4_pin_mask(index);
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;

    switch (config->trigger) {
    case HAL_EXTI_RISING:
        init.Mode = GPIO_MODE_IT_RISING;
        break;
    case HAL_EXTI_FALLING:
        init.Mode = GPIO_MODE_IT_FALLING;
        break;
    case HAL_EXTI_BOTH:
        init.Mode = GPIO_MODE_IT_RISING_FALLING;
        break;
    default:
        return HAL_INVALID_PARAM;
    }

    HAL_GPIO_Init(port, &init);

    irqn = f4_exti_irqn(index);
    HAL_NVIC_SetPriority(irqn, config->priority, 0U);
    HAL_NVIC_EnableIRQ(irqn);
    __HAL_GPIO_EXTI_CLEAR_IT(f4_pin_mask(index));

    s_f4_exti[index].used = true;
    s_f4_exti[index].pin = pin;
    s_f4_exti[index].callback = config->callback;
    s_f4_exti[index].user_data = config->user_data;

    return HAL_OK;
}

hal_status_t hal_exti_enable(hal_pin_t pin)
{
    uint32_t index = HAL_PIN_GET_INDEX(pin);
    uint16_t mask;

    if (index > 15U) {
        return HAL_INVALID_PARAM;
    }
    mask = f4_pin_mask(index);
    EXTI->IMR |= (uint32_t)mask;
    HAL_NVIC_EnableIRQ(f4_exti_irqn(index));
    return HAL_OK;
}

hal_status_t hal_exti_disable(hal_pin_t pin)
{
    uint32_t index = HAL_PIN_GET_INDEX(pin);
    uint16_t mask;

    if (index > 15U) {
        return HAL_INVALID_PARAM;
    }
    mask = f4_pin_mask(index);
    EXTI->IMR &= ~(uint32_t)mask;
    HAL_NVIC_DisableIRQ(f4_exti_irqn(index));
    return HAL_OK;
}

hal_status_t hal_exti_clear(hal_pin_t pin)
{
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if (index > 15U) {
        return HAL_INVALID_PARAM;
    }
    __HAL_GPIO_EXTI_CLEAR_IT(f4_pin_mask(index));
    HAL_NVIC_ClearPendingIRQ(f4_exti_irqn(index));
    return HAL_OK;
}

hal_status_t hal_exti_deinit(hal_pin_t pin)
{
    GPIO_TypeDef *port = f4_gpio_port(HAL_PIN_GET_PORT(pin));
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }

    (void)hal_exti_disable(pin);
    HAL_GPIO_DeInit(port, f4_pin_mask(index));
    s_f4_exti[index].used = false;
    s_f4_exti[index].callback = NULL;
    s_f4_exti[index].user_data = NULL;
    return HAL_OK;
}

/*
 * 直接提供强定义 HAL_GPIO_EXTI_Callback。
 * 若用户工程中也定义了同名函数，会产生重复符号；此时请删除用户定义，
 * 或改为在用户中断服务函数中直接调用上层回调。
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint32_t i;

    for (i = 0U; i < 16U; i++) {
        if ((GPIO_Pin & f4_pin_mask(i)) != 0U) {
            if (s_f4_exti[i].used && (s_f4_exti[i].callback != NULL)) {
                s_f4_exti[i].callback(s_f4_exti[i].pin,
                                      s_f4_exti[i].user_data);
            }
        }
    }
}

/* ========================================================================== */
/* DMA                                                                        */
/* ========================================================================== */

static uint32_t f4_dma_width(hal_dma_width_t width)
{
    switch (width) {
    case HAL_DMA_WIDTH_16BIT:
        return DMA_MEMORY_DATA_SIZE_HALFWORD;
    case HAL_DMA_WIDTH_32BIT:
        return DMA_MEMORY_DATA_SIZE_WORD;
    case HAL_DMA_WIDTH_8BIT:
    default:
        return DMA_MEMORY_DATA_SIZE_BYTE;
    }
}

static uint32_t f4_dma_priority(hal_dma_priority_t priority)
{
    switch (priority) {
    case HAL_DMA_PRIORITY_MEDIUM:
        return DMA_PRIORITY_MEDIUM;
    case HAL_DMA_PRIORITY_HIGH:
        return DMA_PRIORITY_HIGH;
    case HAL_DMA_PRIORITY_VERY_HIGH:
        return DMA_PRIORITY_VERY_HIGH;
    case HAL_DMA_PRIORITY_LOW:
    default:
        return DMA_PRIORITY_LOW;
    }
}

hal_status_t hal_dma_init(hal_dma_t *handle,
                            const hal_dma_config_t *config)
{
    DMA_HandleTypeDef *hdma;
    uint32_t periph_width;
    uint32_t mem_width;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    hdma = (DMA_HandleTypeDef *)handle->base.instance;
    if ((hdma == NULL) || (hdma->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    memset(&hdma->Init, 0, sizeof(hdma->Init));

    switch (config->direction) {
    case HAL_DMA_MEMORY_TO_MEMORY:
        hdma->Init.Direction = DMA_MEMORY_TO_MEMORY;
        break;
    case HAL_DMA_MEMORY_TO_PERIPH:
        hdma->Init.Direction = DMA_MEMORY_TO_PERIPH;
        break;
    case HAL_DMA_PERIPH_TO_MEMORY:
        hdma->Init.Direction = DMA_PERIPH_TO_MEMORY;
        break;
    default:
        return HAL_INVALID_PARAM;
    }

    periph_width = f4_dma_width(config->src_width);
    mem_width = f4_dma_width(config->dst_width);

    if (config->direction == HAL_DMA_MEMORY_TO_PERIPH) {
        hdma->Init.PeriphInc = config->dst_increment ?
                               DMA_PINC_ENABLE : DMA_PINC_DISABLE;
        hdma->Init.MemInc = config->src_increment ?
                            DMA_MINC_ENABLE : DMA_MINC_DISABLE;
        hdma->Init.PeriphDataAlignment = mem_width;
        hdma->Init.MemDataAlignment = periph_width;
    } else if (config->direction == HAL_DMA_PERIPH_TO_MEMORY) {
        hdma->Init.PeriphInc = config->src_increment ?
                               DMA_PINC_ENABLE : DMA_PINC_DISABLE;
        hdma->Init.MemInc = config->dst_increment ?
                            DMA_MINC_ENABLE : DMA_MINC_DISABLE;
        hdma->Init.PeriphDataAlignment = periph_width;
        hdma->Init.MemDataAlignment = mem_width;
    } else {
        hdma->Init.PeriphInc = config->src_increment ?
                               DMA_PINC_ENABLE : DMA_PINC_DISABLE;
        hdma->Init.MemInc = config->dst_increment ?
                            DMA_MINC_ENABLE : DMA_MINC_DISABLE;
        hdma->Init.PeriphDataAlignment = periph_width;
        hdma->Init.MemDataAlignment = mem_width;
    }

    hdma->Init.Mode = (config->mode == HAL_DMA_MODE_CIRCULAR) ?
                      DMA_CIRCULAR : DMA_NORMAL;
    hdma->Init.Priority = f4_dma_priority(config->priority);

    if (HAL_DMA_Init(hdma) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_dma_deinit(hal_dma_t *handle)
{
    DMA_HandleTypeDef *hdma;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }

    hdma = (DMA_HandleTypeDef *)handle->base.instance;
    if (hdma != NULL) {
        (void)HAL_DMA_DeInit(hdma);
    }

    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_dma_start(hal_dma_t *handle, uintptr_t src,
                             uintptr_t dst, size_t length)
{
    DMA_HandleTypeDef *hdma;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (length == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }

    /*
     * 外设方向需要具体的 DMA 请求号/通道映射，公共 API 无法从 request
     * 字段安全推导，返回 NOT_SUPPORTED。M2M 可直接启动。
     */
    if (handle->config.direction != HAL_DMA_MEMORY_TO_MEMORY) {
        return HAL_NOT_SUPPORTED;
    }

    hdma = (DMA_HandleTypeDef *)handle->base.instance;
    if (hdma == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY;
    status = HAL_DMA_Start(hdma, (uint32_t)src, (uint32_t)dst,
                           (uint32_t)length);
    if (status != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return f4_hal_status(status);
    }
    return HAL_OK;
}

hal_status_t hal_dma_stop(hal_dma_t *handle)
{
    DMA_HandleTypeDef *hdma;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }

    hdma = (DMA_HandleTypeDef *)handle->base.instance;
    if (hdma == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_DMA_Abort(hdma);
    handle->base.state = HAL_STATE_READY;
    return f4_hal_status(status);
}

hal_status_t hal_dma_suspend(hal_dma_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_dma_resume(hal_dma_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

size_t hal_dma_get_remaining(hal_dma_t *handle)
{
    DMA_HandleTypeDef *hdma;

    if ((handle == NULL) || (handle->base.instance == NULL)) {
        return 0U;
    }
    hdma = (DMA_HandleTypeDef *)handle->base.instance;
    return (size_t)__HAL_DMA_GET_COUNTER(hdma);
}

/* ========================================================================== */
/* UART                                                                       */
/* ========================================================================== */

static uint32_t f4_uart_mode(hal_uart_mode_t mode)
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

hal_status_t hal_uart_init(hal_uart_t *handle,
                             const hal_uart_config_t *config)
{
    UART_HandleTypeDef *huart;

    if ((handle == NULL) || (config == NULL) || (config->baudrate == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    huart = (UART_HandleTypeDef *)handle->base.instance;
    if ((huart == NULL) || (huart->Instance == NULL)) {
        return HAL_NOT_READY;
    }

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
    huart->Init.Mode = f4_uart_mode(config->mode);
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

hal_status_t hal_uart_deinit(hal_uart_t *handle)
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

hal_status_t hal_uart_transmit(hal_uart_t *handle, const uint8_t *data,
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
    return f4_hal_status(status);
}

hal_status_t hal_uart_receive(hal_uart_t *handle, uint8_t *data,
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
    return f4_hal_status(status);
}

hal_status_t hal_uart_transmit_it(hal_uart_t *handle,
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
    return f4_hal_status(status);
}

hal_status_t hal_uart_receive_it(hal_uart_t *handle, uint8_t *data,
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
    return f4_hal_status(status);
}

hal_status_t hal_uart_transmit_dma(hal_uart_t *handle,
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
    return f4_hal_status(status);
}

hal_status_t hal_uart_receive_dma(hal_uart_t *handle, uint8_t *data,
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
    return f4_hal_status(status);
}

hal_status_t hal_uart_receive_to_idle(hal_uart_t *handle,
                                        uint8_t *data, size_t length,
                                        hal_transfer_mode_t mode)
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

hal_status_t hal_uart_abort_tx(hal_uart_t *handle)
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
    return f4_hal_status(HAL_UART_AbortTransmit(huart));
}

hal_status_t hal_uart_abort_rx(hal_uart_t *handle)
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
    return f4_hal_status(HAL_UART_AbortReceive(huart));
}

hal_status_t hal_uart_abort(hal_uart_t *handle)
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
    return f4_hal_status(HAL_UART_Abort(huart));
}

hal_state_t hal_uart_get_state(hal_uart_t *handle)
{
    return (handle != NULL) ? handle->base.state : HAL_STATE_RESET;
}

hal_status_t hal_uart_get_error(hal_uart_t *handle)
{
    return (handle != NULL) ? handle->base.error : HAL_INVALID_PARAM;
}

hal_status_t hal_uart_register_callback(hal_uart_t *handle,
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
 *   void USART1_IRQHandler(void) { iot_stm32_uart_irq_handler(&huart1_port); }
 * 本函数内部调用 HAL_UART_IRQHandler()，然后按状态派发 PORT 回调。
 */
void iot_stm32_uart_irq_handler(hal_uart_t *handle)
{
    UART_HandleTypeDef *huart;
    hal_state_t previous;

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

/* ========================================================================== */
/* SPI                                                                        */
/* ========================================================================== */

static uint32_t f4_spi_prescaler(SPI_TypeDef *instance, uint32_t baudrate)
{
    uint32_t pclk;
    uint32_t divider;

    if (baudrate == 0U) {
        return SPI_BAUDRATEPRESCALER_2;
    }

    pclk = (instance == SPI1) ? HAL_RCC_GetPCLK2Freq() :
                                HAL_RCC_GetPCLK1Freq();
    divider = (pclk + baudrate - 1U) / baudrate;
    if (divider <= 2U) {
        return SPI_BAUDRATEPRESCALER_2;
    }
    if (divider <= 4U) {
        return SPI_BAUDRATEPRESCALER_4;
    }
    if (divider <= 8U) {
        return SPI_BAUDRATEPRESCALER_8;
    }
    if (divider <= 16U) {
        return SPI_BAUDRATEPRESCALER_16;
    }
    if (divider <= 32U) {
        return SPI_BAUDRATEPRESCALER_32;
    }
    if (divider <= 64U) {
        return SPI_BAUDRATEPRESCALER_64;
    }
    if (divider <= 128U) {
        return SPI_BAUDRATEPRESCALER_128;
    }
    return SPI_BAUDRATEPRESCALER_256;
}

hal_status_t hal_spi_init(hal_spi_t *handle,
                            const hal_spi_config_t *config)
{
    SPI_HandleTypeDef *hspi;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if ((config->data_bits != 8U) && (config->data_bits != 16U)) {
        return HAL_INVALID_PARAM;
    }

    hspi = (SPI_HandleTypeDef *)handle->base.instance;
    if ((hspi == NULL) || (hspi->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    memset(&hspi->Init, 0, sizeof(hspi->Init));
    hspi->Init.Mode = (config->mode == HAL_SPI_MODE_SLAVE) ?
                      SPI_MODE_SLAVE : SPI_MODE_MASTER;
    switch (config->direction) {
    case HAL_SPI_DIRECTION_2LINES_RXONLY:
        hspi->Init.Direction = SPI_DIRECTION_2LINES_RXONLY;
        break;
    case HAL_SPI_DIRECTION_1LINE:
        hspi->Init.Direction = SPI_DIRECTION_1LINE;
        break;
    case HAL_SPI_DIRECTION_2LINES:
    default:
        hspi->Init.Direction = SPI_DIRECTION_2LINES;
        break;
    }
    hspi->Init.DataSize = (config->data_bits == 16U) ?
                          SPI_DATASIZE_16BIT : SPI_DATASIZE_8BIT;
    hspi->Init.CLKPolarity = (config->cpol == HAL_SPI_CPOL_HIGH) ?
                             SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
    hspi->Init.CLKPhase = (config->cpha == HAL_SPI_CPHA_2EDGE) ?
                          SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
    hspi->Init.NSS = SPI_NSS_SOFT;
    hspi->Init.FirstBit = (config->bit_order == HAL_SPI_BITORDER_LSB) ?
                          SPI_FIRSTBIT_LSB : SPI_FIRSTBIT_MSB;
    hspi->Init.BaudRatePrescaler = f4_spi_prescaler(hspi->Instance,
                                                    config->baudrate);
    hspi->Init.CRCPolynomial = 7U;

    if (HAL_SPI_Init(hspi) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_spi_deinit(hal_spi_t *handle)
{
    SPI_HandleTypeDef *hspi;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    hspi = (SPI_HandleTypeDef *)handle->base.instance;
    if (hspi != NULL) {
        (void)HAL_SPI_DeInit(hspi);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_spi_transmit(hal_spi_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    SPI_HandleTypeDef *hspi;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hspi = (SPI_HandleTypeDef *)handle->base.instance;
    if (hspi == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_TX;
    status = HAL_SPI_Transmit(hspi, (uint8_t *)data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return f4_hal_status(status);
}

hal_status_t hal_spi_receive(hal_spi_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    SPI_HandleTypeDef *hspi;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hspi = (SPI_HandleTypeDef *)handle->base.instance;
    if (hspi == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_RX;
    status = HAL_SPI_Receive(hspi, (uint8_t *)data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return f4_hal_status(status);
}

hal_status_t hal_spi_transfer(hal_spi_t *handle, const void *tx_data,
                                void *rx_data, size_t length, uint32_t timeout)
{
    SPI_HandleTypeDef *hspi;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (tx_data == NULL) || (rx_data == NULL) ||
        (length == 0U) || (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hspi = (SPI_HandleTypeDef *)handle->base.instance;
    if (hspi == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_TX_RX;
    status = HAL_SPI_TransmitReceive(hspi, (uint8_t *)tx_data,
                                     (uint8_t *)rx_data,
                                     (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return f4_hal_status(status);
}

hal_status_t hal_spi_transmit_dma(hal_spi_t *handle,
                                    const void *data, size_t length)
{
    SPI_HandleTypeDef *hspi;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hspi = (SPI_HandleTypeDef *)handle->base.instance;
    if (hspi == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_SPI_Transmit_DMA(hspi, (uint8_t *)data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX;
    }
    return f4_hal_status(status);
}

hal_status_t hal_spi_receive_dma(hal_spi_t *handle, void *data,
                                   size_t length)
{
    SPI_HandleTypeDef *hspi;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hspi = (SPI_HandleTypeDef *)handle->base.instance;
    if (hspi == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_SPI_Receive_DMA(hspi, (uint8_t *)data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_RX;
    }
    return f4_hal_status(status);
}

hal_status_t hal_spi_transfer_dma(hal_spi_t *handle,
                                    const void *tx_data, void *rx_data,
                                    size_t length)
{
    SPI_HandleTypeDef *hspi;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (tx_data == NULL) || (rx_data == NULL) ||
        (length == 0U) || (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hspi = (SPI_HandleTypeDef *)handle->base.instance;
    if (hspi == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_SPI_TransmitReceive_DMA(hspi, (uint8_t *)tx_data,
                                         (uint8_t *)rx_data,
                                         (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX_RX;
    }
    return f4_hal_status(status);
}

hal_status_t hal_spi_set_speed(hal_spi_t *handle, uint32_t baudrate)
{
    SPI_HandleTypeDef *hspi;

    if ((handle == NULL) || (baudrate == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (handle->base.state != HAL_STATE_READY) {
        return HAL_BUSY;
    }

    hspi = (SPI_HandleTypeDef *)handle->base.instance;
    if (hspi == NULL) {
        return HAL_NOT_READY;
    }

    hspi->Init.BaudRatePrescaler = f4_spi_prescaler(hspi->Instance, baudrate);
    if (HAL_SPI_Init(hspi) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }
    handle->config.baudrate = baudrate;
    return HAL_OK;
}

hal_status_t hal_spi_abort(hal_spi_t *handle)
{
    SPI_HandleTypeDef *hspi;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hspi = (SPI_HandleTypeDef *)handle->base.instance;
    if (hspi == NULL) {
        return HAL_NOT_READY;
    }

    (void)HAL_SPI_DeInit(hspi);
    handle->base.state = HAL_STATE_READY;
    return f4_hal_status(HAL_SPI_Init(hspi));
}

hal_state_t hal_spi_get_state(hal_spi_t *handle)
{
    return (handle != NULL) ? handle->base.state : HAL_STATE_RESET;
}

/* ========================================================================== */
/* I2C                                                                        */
/* ========================================================================== */

static uint16_t f4_i2c_dev_address(hal_i2c_t *handle, uint16_t address)
{
    if ((handle != NULL) &&
        (handle->config.address_mode == HAL_I2C_ADDR_10BIT)) {
        return address;
    }
    return (uint16_t)(address << 1);
}

hal_status_t hal_i2c_init(hal_i2c_t *handle,
                            const hal_i2c_config_t *config)
{
    I2C_HandleTypeDef *hi2c;
    uint32_t clock_speed;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    hi2c = (I2C_HandleTypeDef *)handle->base.instance;
    if ((hi2c == NULL) || (hi2c->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    switch (config->speed) {
    case HAL_I2C_SPEED_FAST:
        clock_speed = 400000U;
        break;
    case HAL_I2C_SPEED_FAST_PLUS:
        clock_speed = 1000000U;
        break;
    case HAL_I2C_SPEED_STANDARD:
    default:
        clock_speed = 100000U;
        break;
    }

    memset(&hi2c->Init, 0, sizeof(hi2c->Init));
    hi2c->Init.ClockSpeed = clock_speed;
    hi2c->Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c->Init.OwnAddress1 = config->own_address;
    hi2c->Init.AddressingMode =
        (config->address_mode == HAL_I2C_ADDR_10BIT) ?
        I2C_ADDRESSINGMODE_10BIT : I2C_ADDRESSINGMODE_7BIT;
    hi2c->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c->Init.OwnAddress2 = 0U;
    hi2c->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(hi2c) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_i2c_deinit(hal_i2c_t *handle)
{
    I2C_HandleTypeDef *hi2c;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    hi2c = (I2C_HandleTypeDef *)handle->base.instance;
    if (hi2c != NULL) {
        (void)HAL_I2C_DeInit(hi2c);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_i2c_master_transmit(hal_i2c_t *handle,
                                       uint16_t address, const uint8_t *data,
                                       size_t length, uint32_t timeout)
{
    I2C_HandleTypeDef *hi2c;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2c = (I2C_HandleTypeDef *)handle->base.instance;
    if (hi2c == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_TX;
    status = HAL_I2C_Master_Transmit(hi2c, f4_i2c_dev_address(handle, address),
                                     (uint8_t *)data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return f4_hal_status(status);
}

hal_status_t hal_i2c_master_receive(hal_i2c_t *handle,
                                      uint16_t address, uint8_t *data,
                                      size_t length, uint32_t timeout)
{
    I2C_HandleTypeDef *hi2c;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2c = (I2C_HandleTypeDef *)handle->base.instance;
    if (hi2c == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_RX;
    status = HAL_I2C_Master_Receive(hi2c, f4_i2c_dev_address(handle, address),
                                    data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return f4_hal_status(status);
}

hal_status_t hal_i2c_master_transfer(hal_i2c_t *handle,
                                       uint16_t address,
                                       const uint8_t *tx_data, size_t tx_length,
                                       uint8_t *rx_data, size_t rx_length,
                                       uint32_t timeout)
{
    I2C_HandleTypeDef *hi2c;
    HAL_StatusTypeDef status;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if ((tx_length > 0U) && (tx_data == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if ((rx_length > 0U) && (rx_data == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if ((tx_length > 0xFFFFU) || (rx_length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }

    hi2c = (I2C_HandleTypeDef *)handle->base.instance;
    if (hi2c == NULL) {
        return HAL_NOT_READY;
    }

    if ((tx_length == 0U) && (rx_length == 0U)) {
        return HAL_OK;
    }

    /*
     * 当 tx 长度为 1 或 2 时按“寄存器地址 + 数据”处理，使用 HAL_I2C_Mem_Read
     * 产生 Repeated Start。其它组合退化为两次独立传输（中间有 STOP）。
     */
    if ((rx_length > 0U) && ((tx_length == 1U) || (tx_length == 2U))) {
        uint16_t mem_address;
        uint16_t mem_size;

        if (tx_length == 1U) {
            mem_address = tx_data[0];
            mem_size = I2C_MEMADD_SIZE_8BIT;
        } else {
            mem_address = (uint16_t)(((uint16_t)tx_data[0] << 8) | tx_data[1]);
            mem_size = I2C_MEMADD_SIZE_16BIT;
        }

        handle->base.state = HAL_STATE_BUSY_TX_RX;
        status = HAL_I2C_Mem_Read(hi2c, f4_i2c_dev_address(handle, address),
                                  mem_address, mem_size, rx_data,
                                  (uint16_t)rx_length, timeout);
        handle->base.state = (status == HAL_OK) ? HAL_STATE_READY :
                                                 HAL_STATE_ERROR;
        return f4_hal_status(status);
    }

    if (tx_length > 0U) {
        handle->base.state = HAL_STATE_BUSY_TX;
        status = HAL_I2C_Master_Transmit(hi2c,
                                         f4_i2c_dev_address(handle, address),
                                         (uint8_t *)tx_data, (uint16_t)tx_length,
                                         timeout);
        if (status != HAL_OK) {
            handle->base.state = HAL_STATE_ERROR;
            return f4_hal_status(status);
        }
    }

    if (rx_length > 0U) {
        handle->base.state = HAL_STATE_BUSY_RX;
        status = HAL_I2C_Master_Receive(hi2c,
                                        f4_i2c_dev_address(handle, address),
                                        rx_data, (uint16_t)rx_length, timeout);
        if (status != HAL_OK) {
            handle->base.state = HAL_STATE_ERROR;
            return f4_hal_status(status);
        }
    }

    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

hal_status_t hal_i2c_mem_write(hal_i2c_t *handle, uint16_t address,
                                 uint16_t reg, uint8_t reg_size,
                                 const uint8_t *data, size_t length,
                                 uint32_t timeout)
{
    I2C_HandleTypeDef *hi2c;
    HAL_StatusTypeDef status;
    uint16_t mem_size;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if ((reg_size != 1U) && (reg_size != 2U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2c = (I2C_HandleTypeDef *)handle->base.instance;
    if (hi2c == NULL) {
        return HAL_NOT_READY;
    }

    mem_size = (reg_size == 2U) ? I2C_MEMADD_SIZE_16BIT :
                                  I2C_MEMADD_SIZE_8BIT;
    handle->base.state = HAL_STATE_BUSY_TX;
    status = HAL_I2C_Mem_Write(hi2c, f4_i2c_dev_address(handle, address), reg,
                               mem_size, (uint8_t *)data, (uint16_t)length,
                               timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return f4_hal_status(status);
}

hal_status_t hal_i2c_mem_read(hal_i2c_t *handle, uint16_t address,
                                uint16_t reg, uint8_t reg_size, uint8_t *data,
                                size_t length, uint32_t timeout)
{
    I2C_HandleTypeDef *hi2c;
    HAL_StatusTypeDef status;
    uint16_t mem_size;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if ((reg_size != 1U) && (reg_size != 2U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2c = (I2C_HandleTypeDef *)handle->base.instance;
    if (hi2c == NULL) {
        return HAL_NOT_READY;
    }

    mem_size = (reg_size == 2U) ? I2C_MEMADD_SIZE_16BIT :
                                  I2C_MEMADD_SIZE_8BIT;
    handle->base.state = HAL_STATE_BUSY_RX;
    status = HAL_I2C_Mem_Read(hi2c, f4_i2c_dev_address(handle, address), reg,
                              mem_size, data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return f4_hal_status(status);
}

hal_status_t hal_i2c_master_transmit_dma(hal_i2c_t *handle,
                                           uint16_t address,
                                           const uint8_t *data, size_t length)
{
    I2C_HandleTypeDef *hi2c;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2c = (I2C_HandleTypeDef *)handle->base.instance;
    if (hi2c == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_I2C_Master_Transmit_DMA(hi2c,
                                         f4_i2c_dev_address(handle, address),
                                         (uint8_t *)data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX;
    }
    return f4_hal_status(status);
}

hal_status_t hal_i2c_master_receive_dma(hal_i2c_t *handle,
                                          uint16_t address, uint8_t *data,
                                          size_t length)
{
    I2C_HandleTypeDef *hi2c;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2c = (I2C_HandleTypeDef *)handle->base.instance;
    if (hi2c == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_I2C_Master_Receive_DMA(hi2c,
                                        f4_i2c_dev_address(handle, address),
                                        data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_RX;
    }
    return f4_hal_status(status);
}

hal_status_t hal_i2c_is_device_ready(hal_i2c_t *handle,
                                       uint16_t address, uint32_t trials,
                                       uint32_t timeout)
{
    I2C_HandleTypeDef *hi2c;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hi2c = (I2C_HandleTypeDef *)handle->base.instance;
    if (hi2c == NULL) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(HAL_I2C_IsDeviceReady(
        hi2c, f4_i2c_dev_address(handle, address), trials, timeout));
}

hal_status_t hal_i2c_bus_recover(hal_i2c_t *handle)
{
    HAL_UNUSED(handle);
    /* 需要用户提供 SCL/SDA 引脚信息才能做时钟翻转恢复。 */
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_i2c_abort(hal_i2c_t *handle)
{
    I2C_HandleTypeDef *hi2c;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hi2c = (I2C_HandleTypeDef *)handle->base.instance;
    if (hi2c == NULL) {
        return HAL_NOT_READY;
    }

    (void)HAL_I2C_DeInit(hi2c);
    handle->base.state = HAL_STATE_READY;
    return f4_hal_status(HAL_I2C_Init(hi2c));
}

hal_state_t hal_i2c_get_state(hal_i2c_t *handle)
{
    return (handle != NULL) ? handle->base.state : HAL_STATE_RESET;
}

/* ========================================================================== */
/* I2S                                                                        */
/* ========================================================================== */

hal_status_t hal_i2s_init(hal_i2s_t *handle,
                            const hal_i2s_config_t *config)
{
    I2S_HandleTypeDef *hi2s;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if ((hi2s == NULL) || (hi2s->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    memset(&hi2s->Init, 0, sizeof(hi2s->Init));

    switch (config->mode) {
    case HAL_I2S_MODE_MASTER_RX:
        hi2s->Init.Mode = I2S_MODE_MASTER_RX;
        break;
    case HAL_I2S_MODE_SLAVE_TX:
        hi2s->Init.Mode = I2S_MODE_SLAVE_TX;
        break;
    case HAL_I2S_MODE_SLAVE_RX:
        hi2s->Init.Mode = I2S_MODE_SLAVE_RX;
        break;
    case HAL_I2S_MODE_MASTER_TX:
    default:
        hi2s->Init.Mode = I2S_MODE_MASTER_TX;
        break;
    }

    switch (config->standard) {
    case HAL_I2S_STANDARD_MSB:
        hi2s->Init.Standard = I2S_STANDARD_MSB;
        break;
    case HAL_I2S_STANDARD_LSB:
        hi2s->Init.Standard = I2S_STANDARD_LSB;
        break;
    case HAL_I2S_STANDARD_PCM:
        hi2s->Init.Standard = I2S_STANDARD_PCM_SHORT;
        break;
    case HAL_I2S_STANDARD_PHILIPS:
    default:
        hi2s->Init.Standard = I2S_STANDARD_PHILIPS;
        break;
    }

    switch (config->data_size) {
    case HAL_I2S_DATA_24BIT:
        hi2s->Init.DataFormat = I2S_DATAFORMAT_24B;
        break;
    case HAL_I2S_DATA_32BIT:
        hi2s->Init.DataFormat = I2S_DATAFORMAT_32B;
        break;
    case HAL_I2S_DATA_16BIT:
    default:
        hi2s->Init.DataFormat = I2S_DATAFORMAT_16B;
        break;
    }

    hi2s->Init.MCLKOutput = config->master_clock ?
                            I2S_MCLKOUTPUT_ENABLE : I2S_MCLKOUTPUT_DISABLE;
    hi2s->Init.AudioFreq = config->sample_rate;
    hi2s->Init.CPOL = I2S_CPOL_LOW;

    if (HAL_I2S_Init(hi2s) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_i2s_deinit(hal_i2s_t *handle)
{
    I2S_HandleTypeDef *hi2s;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if (hi2s != NULL) {
        (void)HAL_I2S_DeInit(hi2s);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_i2s_transmit(hal_i2s_t *handle, const void *data,
                                size_t length, uint32_t timeout)
{
    I2S_HandleTypeDef *hi2s;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if (hi2s == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_TX;
    status = HAL_I2S_Transmit(hi2s, (uint16_t *)data, (uint16_t)length,
                              timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return f4_hal_status(status);
}

hal_status_t hal_i2s_receive(hal_i2s_t *handle, void *data,
                               size_t length, uint32_t timeout)
{
    I2S_HandleTypeDef *hi2s;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if (hi2s == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_RX;
    status = HAL_I2S_Receive(hi2s, (uint16_t *)data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return f4_hal_status(status);
}

hal_status_t hal_i2s_transmit_dma(hal_i2s_t *handle,
                                    const void *data, size_t length)
{
    I2S_HandleTypeDef *hi2s;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if (hi2s == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_I2S_Transmit_DMA(hi2s, (uint16_t *)data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX;
    }
    return f4_hal_status(status);
}

hal_status_t hal_i2s_receive_dma(hal_i2s_t *handle, void *data,
                                   size_t length)
{
    I2S_HandleTypeDef *hi2s;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (data == NULL) || (length == 0U) ||
        (length > 0xFFFFU)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if (hi2s == NULL) {
        return HAL_NOT_READY;
    }

    status = HAL_I2S_Receive_DMA(hi2s, (uint16_t *)data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_RX;
    }
    return f4_hal_status(status);
}

hal_status_t hal_i2s_abort(hal_i2s_t *handle)
{
    I2S_HandleTypeDef *hi2s;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hi2s = (I2S_HandleTypeDef *)handle->base.instance;
    if (hi2s == NULL) {
        return HAL_NOT_READY;
    }

    (void)HAL_I2S_DeInit(hi2s);
    handle->base.state = HAL_STATE_READY;
    return f4_hal_status(HAL_I2S_Init(hi2s));
}

/* ========================================================================== */
/* CAN                                                                        */
/* ========================================================================== */

#define F4_CAN_MAX_FILTER_BANK 27U

static uint32_t f4_can_mode(hal_can_mode_t mode)
{
    switch (mode) {
    case HAL_CAN_MODE_LOOPBACK:
        return CAN_MODE_LOOPBACK;
    case HAL_CAN_MODE_SILENT:
        return CAN_MODE_SILENT;
    case HAL_CAN_MODE_SILENT_LOOPBACK:
        return CAN_MODE_SILENT_LOOPBACK;
    case HAL_CAN_MODE_NORMAL:
    default:
        return CAN_MODE_NORMAL;
    }
}

static void f4_can_timing(uint32_t bitrate, uint32_t *prescaler,
                          uint32_t *bs1, uint32_t *bs2, uint32_t *sjw)
{
    uint32_t pclk = HAL_RCC_GetPCLK1Freq();
    uint32_t tq_total;
    uint32_t presc = 1U;
    uint32_t tqs;

    if (bitrate == 0U) {
        bitrate = 500000U;
    }
    tq_total = pclk / bitrate;
    if (tq_total < 4U) {
        tq_total = 4U;
    }

    while (((tq_total / presc) > 25U) && (presc < 1024U)) {
        presc++;
    }

    tqs = tq_total / presc;
    if (tqs < 4U) {
        tqs = 4U;
    }
    if (tqs > 25U) {
        tqs = 25U;
    }

    *prescaler = presc;
    *bs1 = (tqs > 3U) ? (tqs - 3U) : 1U;
    *bs2 = 2U;
    *sjw = 1U;
}

static void f4_can_frame_to_header(const hal_can_frame_t *frame,
                                   CAN_TxHeaderTypeDef *header)
{
    memset(header, 0, sizeof(*header));
    if (frame->id_type == HAL_CAN_ID_EXTENDED) {
        header->ExtId = frame->id & 0x1FFFFFFFU;
        header->IDE = CAN_ID_EXT;
    } else {
        header->StdId = frame->id & 0x7FFU;
        header->IDE = CAN_ID_STD;
    }
    header->RTR = (frame->frame_type == HAL_CAN_FRAME_REMOTE) ?
                  CAN_RTR_REMOTE : CAN_RTR_DATA;
    header->DLC = frame->dlc;
    header->TransmitGlobalTime = DISABLE;
}

hal_status_t hal_can_init(hal_can_t *handle,
                            const hal_can_config_t *config)
{
    CAN_HandleTypeDef *hcan;
    uint32_t prescaler;
    uint32_t bs1;
    uint32_t bs2;
    uint32_t sjw;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if (config->enable_fd) {
        return HAL_NOT_SUPPORTED;
    }

    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if ((hcan == NULL) || (hcan->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    f4_can_timing(config->bitrate, &prescaler, &bs1, &bs2, &sjw);

    memset(&hcan->Init, 0, sizeof(hcan->Init));
    hcan->Init.Prescaler = prescaler;
    hcan->Init.Mode = f4_can_mode(config->mode);
    hcan->Init.SJW = sjw;
    hcan->Init.BS1 = bs1;
    hcan->Init.BS2 = bs2;
    hcan->Init.TTCM = DISABLE;
    hcan->Init.ABOM = ENABLE;
    hcan->Init.AWUM = ENABLE;
    hcan->Init.NART = DISABLE;
    hcan->Init.RFLM = DISABLE;
    hcan->Init.TXFP = DISABLE;

    if (HAL_CAN_Init(hcan) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    {
        uint32_t *bank_ptr = f4_can_filter_bank(handle);

        if (bank_ptr != NULL) {
            *bank_ptr = 0U;
        }
    }

    (void)HAL_CAN_ActivateNotification(
        hcan, CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_TX_MAILBOX_EMPTY |
              CAN_IT_ERROR);
    return HAL_OK;
}

hal_status_t hal_can_deinit(hal_can_t *handle)
{
    CAN_HandleTypeDef *hcan;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan != NULL) {
        (void)HAL_CAN_Stop(hcan);
        (void)HAL_CAN_DeInit(hcan);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_can_start(hal_can_t *handle)
{
    CAN_HandleTypeDef *hcan;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(HAL_CAN_Start(hcan));
}

hal_status_t hal_can_stop(hal_can_t *handle)
{
    CAN_HandleTypeDef *hcan;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(HAL_CAN_Stop(hcan));
}

hal_status_t hal_can_transmit(hal_can_t *handle,
                                const hal_can_frame_t *frame,
                                uint32_t timeout)
{
    CAN_HandleTypeDef *hcan;
    CAN_TxHeaderTypeDef header;
    uint32_t mailbox;
    uint32_t start_tick;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (frame == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if ((frame->fd) || (frame->dlc > 8U)) {
        return HAL_NOT_SUPPORTED;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }

    f4_can_frame_to_header(frame, &header);
    start_tick = HAL_GetTick();

    for (;;) {
        status = HAL_CAN_AddTxMessage(hcan, &header,
                                      (uint8_t *)frame->data, &mailbox);
        if (status == HAL_OK) {
            return HAL_OK;
        }
        if (timeout == HAL_TIMEOUT_NONE) {
            return HAL_TIMEOUT;
        }
        if ((timeout != HAL_TIMEOUT_FOREVER) &&
            ((HAL_GetTick() - start_tick) >= timeout)) {
            return HAL_TIMEOUT;
        }
    }
}

hal_status_t hal_can_receive(hal_can_t *handle,
                               hal_can_frame_t *frame, uint32_t timeout)
{
    CAN_HandleTypeDef *hcan;
    CAN_RxHeaderTypeDef header;
    uint32_t start_tick;

    if ((handle == NULL) || (frame == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }

    start_tick = HAL_GetTick();

    for (;;) {
        if (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0U) {
            memset(frame, 0, sizeof(*frame));
            if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &header,
                                     frame->data) != HAL_OK) {
                return HAL_ERROR;
            }
            frame->id = (header.IDE == CAN_ID_EXT) ?
                        header.ExtId : header.StdId;
            frame->id_type = (header.IDE == CAN_ID_EXT) ?
                             HAL_CAN_ID_EXTENDED : HAL_CAN_ID_STANDARD;
            frame->frame_type = (header.RTR == CAN_RTR_REMOTE) ?
                                HAL_CAN_FRAME_REMOTE : HAL_CAN_FRAME_DATA;
            frame->dlc = (uint8_t)header.DLC;
            frame->fd = false;
            frame->brs = false;
            return HAL_OK;
        }

        if (timeout == HAL_TIMEOUT_NONE) {
            return HAL_TIMEOUT;
        }
        if ((timeout != HAL_TIMEOUT_FOREVER) &&
            ((HAL_GetTick() - start_tick) >= timeout)) {
            return HAL_TIMEOUT;
        }
    }
}

hal_status_t hal_can_transmit_it(hal_can_t *handle,
                                   const hal_can_frame_t *frame)
{
    CAN_HandleTypeDef *hcan;
    CAN_TxHeaderTypeDef header;
    uint32_t mailbox;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (frame == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if ((frame->fd) || (frame->dlc > 8U)) {
        return HAL_NOT_SUPPORTED;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }

    f4_can_frame_to_header(frame, &header);
    status = HAL_CAN_AddTxMessage(hcan, &header, (uint8_t *)frame->data,
                                  &mailbox);
    return f4_hal_status(status);
}

#define F4_CAN_FILTER_OWNERS 2U

static struct {
    const hal_can_t *owner;
    uint32_t bank;
} s_can_filter_owners[F4_CAN_FILTER_OWNERS];

static uint32_t *f4_can_filter_bank(const hal_can_t *handle)
{
    uint32_t i;

    for (i = 0U; i < F4_CAN_FILTER_OWNERS; i++) {
        if (s_can_filter_owners[i].owner == handle) {
            return &s_can_filter_owners[i].bank;
        }
    }
    for (i = 0U; i < F4_CAN_FILTER_OWNERS; i++) {
        if (s_can_filter_owners[i].owner == NULL) {
            s_can_filter_owners[i].owner = handle;
            s_can_filter_owners[i].bank = 0U;
            return &s_can_filter_owners[i].bank;
        }
    }
    return NULL;
}

hal_status_t hal_can_add_filter(hal_can_t *handle,
                                  const hal_can_filter_t *filter)
{
    CAN_HandleTypeDef *hcan;
    CAN_FilterTypeDef config;
    uint32_t bank;
    uint32_t *bank_ptr;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (filter == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }

    bank_ptr = f4_can_filter_bank(handle);
    if (bank_ptr == NULL) {
        return HAL_NO_MEMORY;
    }
    bank = *bank_ptr;
    if (bank > F4_CAN_MAX_FILTER_BANK) {
        return HAL_NO_MEMORY;
    }

    memset(&config, 0, sizeof(config));
    config.FilterBank = bank;
    config.FilterMode = CAN_FILTERMODE_IDMASK;
    config.FilterScale = CAN_FILTERSCALE_32BIT;
    config.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    config.FilterActivation = CAN_FILTER_ENABLE;
    config.SlaveStartFilterBank = 14U;

    if (filter->id_type == HAL_CAN_ID_EXTENDED) {
        config.FilterIdHigh = (uint16_t)(filter->id >> 13);
        config.FilterIdLow = (uint16_t)((filter->id << 3) & 0xFFFFU);
        config.FilterMaskIdHigh = (uint16_t)(filter->mask >> 13);
        config.FilterMaskIdLow = (uint16_t)((filter->mask << 3) & 0xFFFFU);
    } else {
        config.FilterIdHigh = (uint16_t)((filter->id << 5) & 0xFFFFU);
        config.FilterIdLow = 0U;
        config.FilterMaskIdHigh = (uint16_t)((filter->mask << 5) & 0xFFFFU);
        config.FilterMaskIdLow = 0U;
    }

    status = HAL_CAN_ConfigFilter(hcan, &config);
    if (status == HAL_OK) {
        *bank_ptr = bank + 1U;
    }
    return f4_hal_status(status);
}

hal_status_t hal_can_clear_filters(hal_can_t *handle)
{
    CAN_HandleTypeDef *hcan;
    CAN_FilterTypeDef config;
    uint32_t bank;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    if (hcan == NULL) {
        return HAL_NOT_READY;
    }

    for (bank = 0U; bank <= F4_CAN_MAX_FILTER_BANK; bank++) {
        memset(&config, 0, sizeof(config));
        config.FilterBank = bank;
        config.FilterActivation = CAN_FILTER_DISABLE;
        (void)HAL_CAN_ConfigFilter(hcan, &config);
    }
    {
        uint32_t *bank_ptr = f4_can_filter_bank(handle);

        if (bank_ptr != NULL) {
            *bank_ptr = 0U;
        }
    }
    return HAL_OK;
}

void iot_stm32_can_irq_handler(hal_can_t *handle)
{
    CAN_HandleTypeDef *hcan;

    if ((handle == NULL) || (handle->base.instance == NULL)) {
        return;
    }
    hcan = (CAN_HandleTypeDef *)handle->base.instance;
    HAL_CAN_IRQHandler(hcan);

    if (hcan->ErrorCode != HAL_CAN_ERROR_NONE) {
        handle->base.error = HAL_ERROR;
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_ERROR, handle->user_data);
        }
        hcan->ErrorCode = HAL_CAN_ERROR_NONE;
    } else if (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0U) {
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_RX_COMPLETE,
                             handle->user_data);
        }
    } else if (HAL_CAN_GetTxMailboxesFreeLevel(hcan) > 0U) {
        if (handle->callback != NULL) {
            handle->callback(handle, HAL_EVENT_TX_COMPLETE,
                             handle->user_data);
        }
    }
}

/* ========================================================================== */
/* ADC                                                                        */
/* ========================================================================== */

hal_status_t hal_adc_init(hal_adc_t *handle,
                            const hal_adc_config_t *config)
{
    ADC_HandleTypeDef *hadc;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if ((hadc == NULL) || (hadc->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    memset(&hadc->Init, 0, sizeof(hadc->Init));
    hadc->Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    switch (config->resolution) {
    case HAL_ADC_RESOLUTION_6BIT:
        hadc->Init.Resolution = ADC_RESOLUTION_6B;
        break;
    case HAL_ADC_RESOLUTION_8BIT:
        hadc->Init.Resolution = ADC_RESOLUTION_8B;
        break;
    case HAL_ADC_RESOLUTION_10BIT:
        hadc->Init.Resolution = ADC_RESOLUTION_10B;
        break;
    case HAL_ADC_RESOLUTION_12BIT:
        hadc->Init.Resolution = ADC_RESOLUTION_12B;
        break;
    default:
        return HAL_NOT_SUPPORTED;
    }
    hadc->Init.DataAlign = (config->alignment == HAL_ADC_ALIGN_LEFT) ?
                           ADC_DATAALIGN_LEFT : ADC_DATAALIGN_RIGHT;
    hadc->Init.ScanConvMode = (config->channel_count > 1U) ? ENABLE : DISABLE;
    hadc->Init.ContinuousConvMode = DISABLE;
    hadc->Init.DiscontinuousConvMode = DISABLE;
    hadc->Init.NbrOfDiscConversion = 0U;
    hadc->Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc->Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc->Init.DMAContinuousRequests = DISABLE;
    hadc->Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc->Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;

    if (HAL_ADC_Init(hadc) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_adc_deinit(hal_adc_t *handle)
{
    ADC_HandleTypeDef *hadc;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc != NULL) {
        (void)HAL_ADC_DeInit(hadc);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_adc_config_channel(hal_adc_t *handle,
                                      const hal_adc_channel_config_t *config)
{
    ADC_HandleTypeDef *hadc;
    ADC_ChannelConfTypeDef channel_config;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if ((config->rank == 0U) || (config->rank > 16U)) {
        return HAL_INVALID_PARAM;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }

    memset(&channel_config, 0, sizeof(channel_config));
    channel_config.Channel = config->channel;
    channel_config.Rank = config->rank;
    channel_config.SamplingTime = (config->sample_time == 0U) ?
                                  ADC_SAMPLETIME_56CYCLES :
                                  config->sample_time;
    return f4_hal_status(HAL_ADC_ConfigChannel(hadc, &channel_config));
}

hal_status_t hal_adc_start(hal_adc_t *handle)
{
    ADC_HandleTypeDef *hadc;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(HAL_ADC_Start(hadc));
}

hal_status_t hal_adc_stop(hal_adc_t *handle)
{
    ADC_HandleTypeDef *hadc;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(HAL_ADC_Stop(hadc));
}

hal_status_t hal_adc_read(hal_adc_t *handle, uint8_t channel,
                            uint32_t *value, uint32_t timeout)
{
    ADC_HandleTypeDef *hadc;
    ADC_ChannelConfTypeDef channel_config;
    HAL_StatusTypeDef status;

    if ((handle == NULL) || (value == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }

    memset(&channel_config, 0, sizeof(channel_config));
    channel_config.Channel = channel;
    channel_config.Rank = 1U;
    channel_config.SamplingTime = ADC_SAMPLETIME_56CYCLES;

    status = HAL_ADC_ConfigChannel(hadc, &channel_config);
    if (status != HAL_OK) {
        return f4_hal_status(status);
    }

    status = HAL_ADC_Start(hadc);
    if (status != HAL_OK) {
        return f4_hal_status(status);
    }

    status = HAL_ADC_PollForConversion(hadc, timeout);
    if (status == HAL_OK) {
        *value = HAL_ADC_GetValue(hadc);
    }
    (void)HAL_ADC_Stop(hadc);
    return f4_hal_status(status);
}

hal_status_t hal_adc_start_it(hal_adc_t *handle)
{
    ADC_HandleTypeDef *hadc;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(HAL_ADC_Start_IT(hadc));
}

hal_status_t hal_adc_start_dma(hal_adc_t *handle,
                                 uint32_t *buffer, size_t length)
{
    ADC_HandleTypeDef *hadc;

    if ((handle == NULL) || (buffer == NULL) || (length == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(HAL_ADC_Start_DMA(hadc, buffer, (uint32_t)length));
}

hal_status_t hal_adc_calibrate(hal_adc_t *handle)
{
    ADC_HandleTypeDef *hadc;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hadc = (ADC_HandleTypeDef *)handle->base.instance;
    if (hadc == NULL) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(HAL_ADCEx_Calibration_Start(hadc));
}

uint32_t hal_adc_get_max_value(hal_adc_t *handle)
{
    uint32_t resolution;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return 0U;
    }
    resolution = (uint32_t)handle->config.resolution;
    if ((resolution == 0U) || (resolution >= 32U)) {
        return 0xFFFFFFFFU;
    }
    return (1UL << resolution) - 1U;
}

/* ========================================================================== */
/* DAC                                                                        */
/* ========================================================================== */

static uint32_t f4_dac_channel(uint8_t channel)
{
    return (channel == 2U) ? DAC_CHANNEL_2 : DAC_CHANNEL_1;
}

static uint32_t f4_dac_alignment(hal_dac_alignment_t alignment)
{
    switch (alignment) {
    case HAL_DAC_ALIGN_8BIT:
        return DAC_ALIGN_8B_R;
    case HAL_DAC_ALIGN_12BIT_LEFT:
        return DAC_ALIGN_12B_L;
    case HAL_DAC_ALIGN_12BIT_RIGHT:
    default:
        return DAC_ALIGN_12B_R;
    }
}

hal_status_t hal_dac_init(hal_dac_t *handle,
                            const hal_dac_config_t *config)
{
    DAC_HandleTypeDef *hdac;
    DAC_ChannelConfTypeDef channel_config;
    uint32_t channel;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if ((config->channel != 1U) && (config->channel != 2U)) {
        return HAL_INVALID_PARAM;
    }

    hdac = (DAC_HandleTypeDef *)handle->base.instance;
    if ((hdac == NULL) || (hdac->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    if (HAL_DAC_Init(hdac) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    channel = f4_dac_channel(config->channel);
    memset(&channel_config, 0, sizeof(channel_config));
    channel_config.DAC_Trigger = DAC_TRIGGER_NONE;
    channel_config.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;

    if (HAL_DAC_ConfigChannel(hdac, &channel_config, channel) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_dac_deinit(hal_dac_t *handle)
{
    DAC_HandleTypeDef *hdac;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    hdac = (DAC_HandleTypeDef *)handle->base.instance;
    if (hdac != NULL) {
        (void)HAL_DAC_DeInit(hdac);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_dac_start(hal_dac_t *handle)
{
    DAC_HandleTypeDef *hdac;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hdac = (DAC_HandleTypeDef *)handle->base.instance;
    if (hdac == NULL) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(
        HAL_DAC_Start(hdac, f4_dac_channel(handle->config.channel)));
}

hal_status_t hal_dac_stop(hal_dac_t *handle)
{
    DAC_HandleTypeDef *hdac;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hdac = (DAC_HandleTypeDef *)handle->base.instance;
    if (hdac == NULL) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(
        HAL_DAC_Stop(hdac, f4_dac_channel(handle->config.channel)));
}

hal_status_t hal_dac_write(hal_dac_t *handle, uint32_t value)
{
    DAC_HandleTypeDef *hdac;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    hdac = (DAC_HandleTypeDef *)handle->base.instance;
    if (hdac == NULL) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(HAL_DAC_SetValue(
        hdac, f4_dac_channel(handle->config.channel),
        f4_dac_alignment(handle->config.alignment), value));
}

hal_status_t hal_dac_write_dma(hal_dac_t *handle,
                                 const uint32_t *buffer, size_t length)
{
    DAC_HandleTypeDef *hdac;

    if ((handle == NULL) || (buffer == NULL) || (length == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    hdac = (DAC_HandleTypeDef *)handle->base.instance;
    if (hdac == NULL) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(HAL_DAC_Start_DMA(
        hdac, f4_dac_channel(handle->config.channel),
        (uint32_t *)(uintptr_t)buffer, (uint32_t)length,
        f4_dac_alignment(handle->config.alignment)));
}

/* ========================================================================== */
/* OPAMP（F4 无此模块）                                                       */
/* ========================================================================== */

hal_status_t hal_opamp_init(hal_opamp_t *handle,
                              const hal_opamp_config_t *config)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(config);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_opamp_start(hal_opamp_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_opamp_stop(hal_opamp_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

/* ========================================================================== */
/* TIM / PWM / CAPTURE / ENCODER                                              */
/* ========================================================================== */

static uint32_t f4_tim_counter_mode(hal_tim_counter_t mode)
{
    switch (mode) {
    case HAL_TIM_COUNTER_DOWN:
        return TIM_COUNTERMODE_DOWN;
    case HAL_TIM_COUNTER_CENTER:
        return TIM_COUNTERMODE_CENTERALIGNED1;
    case HAL_TIM_COUNTER_UP:
    default:
        return TIM_COUNTERMODE_UP;
    }
}

static uint32_t f4_tim_clock_division(uint8_t division)
{
    switch (division) {
    case 2U:
        return TIM_CLOCKDIVISION_DIV2;
    case 4U:
        return TIM_CLOCKDIVISION_DIV4;
    case 1U:
    default:
        return TIM_CLOCKDIVISION_DIV1;
    }
}

static uint32_t f4_tim_channel(uint32_t channel)
{
    switch (channel) {
    case 1U:
        return TIM_CHANNEL_1;
    case 2U:
        return TIM_CHANNEL_2;
    case 3U:
        return TIM_CHANNEL_3;
    case 4U:
        return TIM_CHANNEL_4;
    case 5U:
        return TIM_CHANNEL_5;
    case 6U:
        return TIM_CHANNEL_6;
    default:
        return 0U;
    }
}

static uint32_t f4_tim_ic_prescaler(uint32_t prescaler)
{
    switch (prescaler) {
    case 2U:
        return TIM_ICPSC_DIV2;
    case 4U:
        return TIM_ICPSC_DIV4;
    case 8U:
        return TIM_ICPSC_DIV8;
    case 1U:
    default:
        return TIM_ICPSC_DIV1;
    }
}

hal_status_t hal_tim_init(hal_tim_t *handle,
                            const hal_tim_config_t *config)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    memset(&htim->Init, 0, sizeof(htim->Init));
    htim->Init.Prescaler = config->prescaler;
    htim->Init.CounterMode = f4_tim_counter_mode(config->counter_mode);
    htim->Init.Period = config->period;
    htim->Init.ClockDivision = f4_tim_clock_division(config->clock_division);
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_Base_Init(htim) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_tim_deinit(hal_tim_t *handle)
{
    TIM_HandleTypeDef *htim;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim != NULL) {
        (void)HAL_TIM_Base_DeInit(htim);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_tim_start(hal_tim_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    if (HAL_TIM_Base_Start(htim) == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

hal_status_t hal_tim_stop(hal_tim_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    (void)HAL_TIM_Base_Stop(htim);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

hal_status_t hal_tim_start_it(hal_tim_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    if (HAL_TIM_Base_Start_IT(htim) == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

hal_status_t hal_tim_stop_it(hal_tim_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    (void)HAL_TIM_Base_Stop_IT(htim);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

hal_status_t hal_tim_set_period(hal_tim_t *handle,
                                  uint32_t period)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }

    htim->Init.Period = period;
    __HAL_TIM_SET_AUTORELOAD(htim, period);
    handle->config.period = period;
    return HAL_OK;
}

hal_status_t hal_tim_set_prescaler(hal_tim_t *handle,
                                     uint32_t prescaler)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }

    htim->Init.Prescaler = prescaler;
    __HAL_TIM_SET_PRESCALER(htim, prescaler);
    handle->config.prescaler = prescaler;
    return HAL_OK;
}

hal_status_t hal_tim_set_counter(hal_tim_t *handle,
                                   uint32_t counter)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    __HAL_TIM_SET_COUNTER(htim, counter);
    return HAL_OK;
}

uint32_t hal_tim_get_counter(hal_tim_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.instance == NULL)) {
        return 0U;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    return (uint32_t)__HAL_TIM_GET_COUNTER(htim);
}

static uint32_t f4_timer_clock(TIM_TypeDef *instance)
{
    uint32_t pclk;

#if defined(TIM8)
    if ((instance == TIM1) || (instance == TIM8)) {
        pclk = HAL_RCC_GetPCLK2Freq();
    } else {
        pclk = HAL_RCC_GetPCLK1Freq();
    }
#else
    if (instance == TIM1) {
        pclk = HAL_RCC_GetPCLK2Freq();
    } else {
        pclk = HAL_RCC_GetPCLK1Freq();
    }
#endif

    if (pclk != HAL_RCC_GetHCLKFreq()) {
        pclk *= 2U;
    }
    return pclk;
}

static void f4_pwm_calculate(uint32_t timer_clock, uint32_t frequency,
                             uint32_t *prescaler, uint32_t *period)
{
    uint32_t psc = timer_clock / 1000000U;
    uint32_t per;

    if (psc == 0U) {
        psc = 1U;
    }
    if (psc > 65536U) {
        psc = 65536U;
    }

    per = (timer_clock / psc) / frequency;
    if (per == 0U) {
        per = 1U;
    }
    if (per > 65536U) {
        per = 65536U;
    }

    *prescaler = psc - 1U;
    *period = per - 1U;
}

static hal_status_t f4_pwm_config_output(hal_pwm_t *handle,
                                          TIM_HandleTypeDef *htim,
                                          uint32_t channel)
{
    TIM_OC_InitTypeDef oc;
    uint32_t duty = HAL_MIN(handle->config.duty, 10000U);
    uint32_t period = htim->Init.Period + 1U;

    memset(&oc, 0, sizeof(oc));
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = (period * duty) / 10000U;
    oc.OCPolarity = (handle->config.polarity == HAL_PWM_POLARITY_LOW) ?
                    TIM_OCPOLARITY_LOW : TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCPreload = TIM_OCPRELOAD_ENABLE;

    return f4_hal_status(HAL_TIM_PWM_ConfigChannel(htim, &oc, channel));
}

hal_status_t hal_pwm_init(hal_pwm_t *handle,
                            const hal_pwm_config_t *config)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;
    uint32_t prescaler;
    uint32_t period;

    if ((handle == NULL) || (config == NULL) || (config->frequency == 0U) ||
        (config->duty > 10000U)) {
        return HAL_INVALID_PARAM;
    }
    if ((handle->channel < 1U) || (handle->channel > 4U)) {
        handle->channel = 1U;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    channel = f4_tim_channel(handle->channel);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }

    f4_pwm_calculate(f4_timer_clock(htim->Instance), config->frequency,
                     &prescaler, &period);

    memset(&htim->Init, 0, sizeof(htim->Init));
    htim->Init.Prescaler = prescaler;
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    htim->Init.Period = period;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_PWM_Init(htim) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    if (f4_pwm_config_output(handle, htim, channel) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_pwm_deinit(hal_pwm_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim != NULL) {
        channel = f4_tim_channel(handle->channel);
        if (channel != 0U) {
            (void)HAL_TIM_PWM_Stop(htim, channel);
        }
        (void)HAL_TIM_PWM_DeInit(htim);
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_pwm_start(hal_pwm_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = f4_tim_channel(handle->channel);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (HAL_TIM_PWM_Start(htim, channel) == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

hal_status_t hal_pwm_stop(hal_pwm_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = f4_tim_channel(handle->channel);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }
    (void)HAL_TIM_PWM_Stop(htim, channel);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

hal_status_t hal_pwm_set_frequency(hal_pwm_t *handle,
                                     uint32_t frequency)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;
    uint32_t prescaler;
    uint32_t period;
    bool running;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET) ||
        (frequency == 0U)) {
        return HAL_INVALID_PARAM;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = f4_tim_channel(handle->channel);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }

    running = (handle->base.state == HAL_STATE_BUSY);
    (void)HAL_TIM_PWM_Stop(htim, channel);

    f4_pwm_calculate(f4_timer_clock(htim->Instance), frequency,
                     &prescaler, &period);
    htim->Init.Prescaler = prescaler;
    htim->Init.Period = period;
    if (HAL_TIM_PWM_Init(htim) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config.frequency = frequency;
    if (f4_pwm_config_output(handle, htim, channel) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    if (running) {
        (void)HAL_TIM_PWM_Start(htim, channel);
        handle->base.state = HAL_STATE_BUSY;
    } else {
        handle->base.state = HAL_STATE_READY;
    }
    return HAL_OK;
}

hal_status_t hal_pwm_set_duty(hal_pwm_t *handle, uint32_t duty)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;
    uint32_t compare;
    uint32_t period;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    if (duty > 10000U) {
        duty = 10000U;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = f4_tim_channel(handle->channel);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }

    period = htim->Init.Period + 1U;
    compare = (period * duty) / 10000U;
    __HAL_TIM_SET_COMPARE(htim, channel, compare);
    handle->config.duty = duty;
    return HAL_OK;
}

hal_status_t hal_pwm_set_polarity(hal_pwm_t *handle,
                                    hal_pwm_polarity_t polarity)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = f4_tim_channel(handle->channel);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }

    handle->config.polarity = polarity;
    return f4_pwm_config_output(handle, htim, channel);
}

static uint32_t f4_capture_channel(hal_capture_t *handle)
{
    if (handle == NULL) {
        return 0U;
    }
    return f4_tim_channel(1U);
}

hal_status_t hal_capture_init(hal_capture_t *handle,
                                const hal_capture_config_t *config)
{
    TIM_HandleTypeDef *htim;
    TIM_IC_InitTypeDef ic;
    uint32_t channel;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_NOT_READY;
    }
    channel = f4_capture_channel(handle);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }

    memset(&htim->Init, 0, sizeof(htim->Init));
    htim->Init.Prescaler = 0U;
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    htim->Init.Period = 0xFFFFU;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_IC_Init(htim) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    memset(&ic, 0, sizeof(ic));
    switch (config->edge) {
    case HAL_CAPTURE_FALLING:
        ic.ICPolarity = TIM_ICPOLARITY_FALLING;
        break;
    case HAL_CAPTURE_BOTH:
        ic.ICPolarity = TIM_ICPOLARITY_BOTHEDGE;
        break;
    case HAL_CAPTURE_RISING:
    default:
        ic.ICPolarity = TIM_ICPOLARITY_RISING;
        break;
    }
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = f4_tim_ic_prescaler(config->prescaler);
    ic.ICFilter = (uint32_t)(config->filter & 0x0FU);

    if (HAL_TIM_IC_ConfigChannel(htim, &ic, channel) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_capture_start(hal_capture_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = f4_capture_channel(handle);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (HAL_TIM_IC_Start(htim, channel) == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

hal_status_t hal_capture_stop(hal_capture_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = f4_capture_channel(handle);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }
    (void)HAL_TIM_IC_Stop_IT(htim, channel);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

hal_status_t hal_capture_start_it(hal_capture_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    channel = f4_capture_channel(handle);
    if (channel == 0U) {
        return HAL_INVALID_PARAM;
    }
    if (HAL_TIM_IC_Start_IT(htim, channel) == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

uint32_t hal_capture_get_value(hal_capture_t *handle)
{
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    if ((handle == NULL) || (handle->base.instance == NULL)) {
        return 0U;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    channel = f4_capture_channel(handle);
    if (channel == 0U) {
        return 0U;
    }
    return HAL_TIM_ReadCapturedValue(htim, channel);
}

hal_status_t hal_encoder_init(hal_encoder_t *handle,
                                const hal_encoder_config_t *config)
{
    TIM_HandleTypeDef *htim;
    TIM_Encoder_InitTypeDef encoder;
    uint32_t polarity;
    uint32_t period;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if ((htim == NULL) || (htim->Instance == NULL)) {
        return HAL_NOT_READY;
    }

    period = (config->period == 0U) ? 0xFFFFU : config->period;
    if (period > 0xFFFFU) {
        period = 0xFFFFU;
    }

    memset(&htim->Init, 0, sizeof(htim->Init));
    htim->Init.Prescaler = 0U;
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    htim->Init.Period = period;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    polarity = config->reverse ? TIM_ICPOLARITY_FALLING : TIM_ICPOLARITY_RISING;

    memset(&encoder, 0, sizeof(encoder));
    switch (config->mode) {
    case HAL_ENCODER_MODE_TI1:
        encoder.EncoderMode = TIM_ENCODERMODE_TI1;
        break;
    case HAL_ENCODER_MODE_TI2:
        encoder.EncoderMode = TIM_ENCODERMODE_TI2;
        break;
    case HAL_ENCODER_MODE_TI12:
    default:
        encoder.EncoderMode = TIM_ENCODERMODE_TI12;
        break;
    }
    encoder.IC1Polarity = polarity;
    encoder.IC1Prescaler = TIM_ICPSC_DIV1;
    encoder.IC1Filter = (uint32_t)(config->filter & 0x0FU);
    encoder.IC2Polarity = polarity;
    encoder.IC2Prescaler = TIM_ICPSC_DIV1;
    encoder.IC2Filter = (uint32_t)(config->filter & 0x0FU);

    if (HAL_TIM_Encoder_Init(htim, &encoder) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->config = *config;
    handle->config.period = period;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_encoder_start(hal_encoder_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    if (HAL_TIM_Encoder_Start(htim, TIM_CHANNEL_ALL) == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY;
        return HAL_OK;
    }
    return HAL_ERROR;
}

hal_status_t hal_encoder_stop(hal_encoder_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    (void)HAL_TIM_Encoder_Stop(htim, TIM_CHANNEL_ALL);
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int32_t hal_encoder_get_count(hal_encoder_t *handle)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.instance == NULL)) {
        return 0;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    return (int32_t)__HAL_TIM_GET_COUNTER(htim);
}

hal_status_t hal_encoder_set_count(hal_encoder_t *handle,
                                     int32_t count)
{
    TIM_HandleTypeDef *htim;

    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    htim = (TIM_HandleTypeDef *)handle->base.instance;
    if (htim == NULL) {
        return HAL_NOT_READY;
    }
    __HAL_TIM_SET_COUNTER(htim, (uint32_t)count);
    return HAL_OK;
}

/* ========================================================================== */
/* WDG                                                                        */
/* ========================================================================== */

static uint32_t f4_iwdg_prescaler(uint32_t divider)
{
    switch (divider) {
    case 8U:
        return IWDG_PRESCALER_8;
    case 16U:
        return IWDG_PRESCALER_16;
    case 32U:
        return IWDG_PRESCALER_32;
    case 64U:
        return IWDG_PRESCALER_64;
    case 128U:
        return IWDG_PRESCALER_128;
    case 256U:
        return IWDG_PRESCALER_256;
    case 4U:
    default:
        return IWDG_PRESCALER_4;
    }
}

static uint32_t f4_wwdg_prescaler(uint32_t divider)
{
    switch (divider) {
    case 2U:
        return WWDG_PRESCALER_2;
    case 4U:
        return WWDG_PRESCALER_4;
    case 8U:
        return WWDG_PRESCALER_8;
    case 1U:
    default:
        return WWDG_PRESCALER_1;
    }
}

hal_status_t hal_wdg_init(hal_wdg_t *handle,
                            const hal_wdg_config_t *config)
{
    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    if (config->type == HAL_WDG_INDEPENDENT) {
        IWDG_HandleTypeDef *hiwdg =
            (IWDG_HandleTypeDef *)handle->base.instance;
        uint64_t ticks;
        uint32_t divider = 4U;
        uint32_t reload;

        if (hiwdg == NULL) {
            return HAL_NOT_READY;
        }
        if (hiwdg->Instance == NULL) {
            hiwdg->Instance = IWDG;
        }

        /* LSI 约 40kHz。 */
        ticks = (uint64_t)config->timeout_ms * 40U;
        while ((divider < 256U) && ((ticks / divider) > 4095U)) {
            divider <<= 1;
        }
        reload = (uint32_t)(ticks / divider);
        if (reload == 0U) {
            reload = 1U;
        }
        if (reload > 4095U) {
            reload = 4095U;
        }

        memset(&hiwdg->Init, 0, sizeof(hiwdg->Init));
        hiwdg->Init.Prescaler = f4_iwdg_prescaler(divider);
        hiwdg->Init.Reload = reload;
        hiwdg->Init.Window = 0x0FFFU;

        if (HAL_IWDG_Init(hiwdg) != HAL_OK) {
            handle->base.state = HAL_STATE_ERROR;
            return HAL_ERROR;
        }
    } else {
        WWDG_HandleTypeDef *hwwdg =
            (WWDG_HandleTypeDef *)handle->base.instance;
        uint64_t pclk_ticks;
        uint64_t unit;
        uint32_t divider = 1U;
        uint32_t counter_ticks;
        uint32_t pclk;

        if ((hwwdg == NULL) || (hwwdg->Instance != WWDG)) {
            return HAL_NOT_READY;
        }

        pclk = HAL_RCC_GetPCLK1Freq();
        pclk_ticks = ((uint64_t)config->timeout_ms * pclk) / 1000U;
        while ((divider < 8U) &&
               ((pclk_ticks / divider) > (4096ULL * 64ULL))) {
            divider <<= 1;
        }

        unit = 4096ULL * divider;
        counter_ticks = (uint32_t)(pclk_ticks / unit);
        if (counter_ticks == 0U) {
            counter_ticks = 1U;
        }
        if (counter_ticks > 64U) {
            counter_ticks = 64U;
        }

        memset(&hwwdg->Init, 0, sizeof(hwwdg->Init));
        hwwdg->Init.Prescaler = f4_wwdg_prescaler(divider);
        hwwdg->Init.Counter = (uint32_t)(0x3FU + counter_ticks);
        hwwdg->Init.EWIMode = WWDG_EWI_DISABLE;

        if (config->window_ms == 0U) {
            hwwdg->Init.Window = 0x7FU;
        } else {
            uint32_t window_ticks = (uint32_t)((((uint64_t)config->window_ms *
                                                 pclk) / 1000U) / unit);
            uint32_t window = 0x3FU + window_ticks;
            if (window > hwwdg->Init.Counter) {
                window = hwwdg->Init.Counter;
            }
            if (window < 0x40U) {
                window = 0x40U;
            }
            hwwdg->Init.Window = window;
        }

        if (HAL_WWDG_Init(hwwdg) != HAL_OK) {
            handle->base.state = HAL_STATE_ERROR;
            return HAL_ERROR;
        }
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_wdg_start(hal_wdg_t *handle)
{
    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    /* IWDG/WWDG 在 HAL_xxx_Init() 时已启动。 */
    return HAL_OK;
}

hal_status_t hal_wdg_refresh(hal_wdg_t *handle)
{
    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }

    if (handle->config.type == HAL_WDG_INDEPENDENT) {
        IWDG_HandleTypeDef *hiwdg =
            (IWDG_HandleTypeDef *)handle->base.instance;
        if (hiwdg == NULL) {
            return HAL_NOT_READY;
        }
        return f4_hal_status(HAL_IWDG_Refresh(hiwdg));
    } else {
        WWDG_HandleTypeDef *hwwdg =
            (WWDG_HandleTypeDef *)handle->base.instance;
        if (hwwdg == NULL) {
            return HAL_NOT_READY;
        }
        return f4_hal_status(HAL_WWDG_Refresh(hwwdg));
    }
}

hal_status_t hal_wdg_stop(hal_wdg_t *handle)
{
    HAL_UNUSED(handle);
    /* IWDG 启动后无法停止；WWDG 只能通过复位关闭。 */
    return HAL_NOT_SUPPORTED;
}

bool hal_wdg_is_reset(void)
{
    return (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) ||
           (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET);
}

void hal_wdg_clear_reset_flag(void)
{
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

/* ========================================================================== */
/* RTC                                                                        */
/* ========================================================================== */

static RTC_HandleTypeDef s_f4_rtc;
static bool s_f4_rtc_ready;
static hal_rtc_alarm_callback_t s_f4_rtc_alarm_cb[2];
static void *s_f4_rtc_alarm_user[2];

static int64_t f4_days_from_civil(int32_t year, uint32_t month, uint32_t day)
{
    int32_t era;
    int32_t adjusted_month;
    uint32_t year_of_era;
    uint32_t day_of_year;
    uint32_t day_of_era;

    year -= (month <= 2U) ? 1 : 0;
    era = (year >= 0 ? year : year - 399) / 400;
    year_of_era = (uint32_t)(year - era * 400);
    adjusted_month = (int32_t)month + ((month > 2U) ? -3 : 9);
    day_of_year = (uint32_t)((153 * adjusted_month + 2) / 5) + day - 1U;
    day_of_era = year_of_era * 365U + year_of_era / 4U -
                 year_of_era / 100U + day_of_year;
    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

hal_status_t hal_rtc_init(void)
{
    if (s_f4_rtc_ready) {
        return HAL_OK;
    }

    memset(&s_f4_rtc, 0, sizeof(s_f4_rtc));
    s_f4_rtc.Instance = RTC;
    s_f4_rtc.Init.HourFormat = RTC_HOURFORMAT_24;
    s_f4_rtc.Init.AsynchPrediv = 0x7FU;
    s_f4_rtc.Init.SynchPrediv = 0xFFU;
    s_f4_rtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    s_f4_rtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    s_f4_rtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;

    if (HAL_RTC_Init(&s_f4_rtc) != HAL_OK) {
        return HAL_ERROR;
    }

    s_f4_rtc_ready = true;
    return HAL_OK;
}

hal_status_t hal_rtc_deinit(void)
{
    if (!s_f4_rtc_ready) {
        return HAL_NOT_READY;
    }
    (void)HAL_RTC_DeInit(&s_f4_rtc);
    s_f4_rtc_ready = false;
    s_f4_rtc_alarm_cb[0] = NULL;
    s_f4_rtc_alarm_cb[1] = NULL;
    s_f4_rtc_alarm_user[0] = NULL;
    s_f4_rtc_alarm_user[1] = NULL;
    return HAL_OK;
}

hal_status_t hal_rtc_set_time(const hal_rtc_time_t *time)
{
    RTC_TimeTypeDef rtc_time;
    RTC_DateTypeDef rtc_date;

    if (time == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (!s_f4_rtc_ready) {
        return HAL_NOT_READY;
    }

    memset(&rtc_time, 0, sizeof(rtc_time));
    rtc_time.Hours = time->hour;
    rtc_time.Minutes = time->minute;
    rtc_time.Seconds = time->second;

    if (HAL_RTC_SetTime(&s_f4_rtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }

    memset(&rtc_date, 0, sizeof(rtc_date));
    rtc_date.WeekDay = (time->weekday >= 1U && time->weekday <= 7U) ?
                       time->weekday : 1U;
    rtc_date.Month = time->month;
    rtc_date.Date = time->day;
    rtc_date.Year = (uint8_t)(time->year % 100U);

    if (HAL_RTC_SetDate(&s_f4_rtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_OK;
}

hal_status_t hal_rtc_get_time(hal_rtc_time_t *time)
{
    RTC_TimeTypeDef rtc_time;
    RTC_DateTypeDef rtc_date;

    if (time == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (!s_f4_rtc_ready) {
        return HAL_NOT_READY;
    }

    memset(&rtc_time, 0, sizeof(rtc_time));
    memset(&rtc_date, 0, sizeof(rtc_date));
    if (HAL_RTC_GetTime(&s_f4_rtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_RTC_GetDate(&s_f4_rtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }

    time->year = (uint16_t)(2000U + rtc_date.Year);
    time->month = rtc_date.Month;
    time->day = rtc_date.Date;
    time->weekday = rtc_date.WeekDay;
    time->hour = rtc_time.Hours;
    time->minute = rtc_time.Minutes;
    time->second = rtc_time.Seconds;
    return HAL_OK;
}

hal_status_t hal_rtc_set_alarm(hal_rtc_alarm_t alarm,
                                 const hal_rtc_alarm_config_t *config)
{
    RTC_AlarmTypeDef rtc_alarm;

    if (config == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (!s_f4_rtc_ready) {
        return HAL_NOT_READY;
    }
    if ((alarm != HAL_RTC_ALARM_A) && (alarm != HAL_RTC_ALARM_B)) {
        return HAL_INVALID_PARAM;
    }

    memset(&rtc_alarm, 0, sizeof(rtc_alarm));
    rtc_alarm.AlarmTime.Hours = config->time.hour;
    rtc_alarm.AlarmTime.Minutes = config->time.minute;
    rtc_alarm.AlarmTime.Seconds = config->time.second;
    rtc_alarm.AlarmMask = RTC_ALARMMASK_NONE;
    rtc_alarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
    rtc_alarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
    rtc_alarm.AlarmDateWeekDay = 1U;
    rtc_alarm.Alarm = (alarm == HAL_RTC_ALARM_A) ?
                      RTC_ALARM_A : RTC_ALARM_B;

    if (HAL_RTC_SetAlarm_IT(&s_f4_rtc, &rtc_alarm, RTC_FORMAT_BIN) != HAL_OK) {
        return HAL_ERROR;
    }

    s_f4_rtc_alarm_cb[(alarm == HAL_RTC_ALARM_A) ? 0 : 1] = config->callback;
    s_f4_rtc_alarm_user[(alarm == HAL_RTC_ALARM_A) ? 0 : 1] = config->user_data;
    return HAL_OK;
}

hal_status_t hal_rtc_disable_alarm(hal_rtc_alarm_t alarm)
{
    if (!s_f4_rtc_ready) {
        return HAL_NOT_READY;
    }
    if ((alarm != HAL_RTC_ALARM_A) && (alarm != HAL_RTC_ALARM_B)) {
        return HAL_INVALID_PARAM;
    }

    s_f4_rtc_alarm_cb[(alarm == HAL_RTC_ALARM_A) ? 0 : 1] = NULL;
    s_f4_rtc_alarm_user[(alarm == HAL_RTC_ALARM_A) ? 0 : 1] = NULL;
    return f4_hal_status(HAL_RTC_DeactivateAlarm(
        &s_f4_rtc, (alarm == HAL_RTC_ALARM_A) ? RTC_ALARM_A : RTC_ALARM_B));
}

hal_status_t hal_rtc_get_timestamp(uint32_t *timestamp)
{
    hal_rtc_time_t time;
    int64_t days;
    int64_t seconds;

    if (timestamp == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (hal_rtc_get_time(&time) != HAL_OK) {
        return HAL_NOT_READY;
    }

    days = f4_days_from_civil((int32_t)time.year, time.month, time.day);
    seconds = days * 86400 + (int64_t)time.hour * 3600 +
              (int64_t)time.minute * 60 + time.second;
    *timestamp = (uint32_t)seconds;
    return HAL_OK;
}

void iot_stm32_rtc_alarm_irq_handler(void)
{
    bool alarm_a;
    bool alarm_b;

    if (!s_f4_rtc_ready) {
        return;
    }

    alarm_a = (__HAL_RTC_ALARM_GET_FLAG(&s_f4_rtc, RTC_FLAG_ALRAF) != RESET);
    alarm_b = (__HAL_RTC_ALARM_GET_FLAG(&s_f4_rtc, RTC_FLAG_ALRBF) != RESET);
    HAL_RTC_AlarmIRQHandler(&s_f4_rtc);

    if (alarm_a && (s_f4_rtc_alarm_cb[0] != NULL)) {
        s_f4_rtc_alarm_cb[0](HAL_RTC_ALARM_A, s_f4_rtc_alarm_user[0]);
    }
    if (alarm_b && (s_f4_rtc_alarm_cb[1] != NULL)) {
        s_f4_rtc_alarm_cb[1](HAL_RTC_ALARM_B, s_f4_rtc_alarm_user[1]);
    }
}

/* ========================================================================== */
/* FLASH                                                                      */
/* ========================================================================== */

#ifndef IOT_F4_FLASH_SIZE
#define IOT_F4_FLASH_SIZE (1024U * 1024U)
#endif

#ifndef IOT_F4_FLASH_SECTOR_SIZE
#define IOT_F4_FLASH_SECTOR_SIZE (16U * 1024U)
#endif

static uint32_t f4_flash_sector(uintptr_t address)
{
    uintptr_t offset = address - (uintptr_t)FLASH_BASE;

    if (offset < 0x10000U) {
        return (uint32_t)(offset / 0x4000U);
    }
    if (offset < 0x20000U) {
        return 4U;
    }
    return 5U + (uint32_t)((offset - 0x20000U) / 0x20000U);
}

static uintptr_t f4_flash_sector_start(uint32_t sector)
{
    if (sector < 4U) {
        return (uintptr_t)FLASH_BASE + (uintptr_t)sector * 0x4000U;
    }
    if (sector == 4U) {
        return (uintptr_t)FLASH_BASE + 0x10000U;
    }
    return (uintptr_t)FLASH_BASE + 0x20000U +
           (uintptr_t)(sector - 5U) * 0x20000U;
}

hal_status_t hal_flash_init(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    memset(&handle->info, 0, sizeof(handle->info));
    handle->info.start = (uintptr_t)FLASH_BASE;
    handle->info.size = IOT_F4_FLASH_SIZE;
    handle->info.erase_size = IOT_F4_FLASH_SECTOR_SIZE;
    handle->info.program_size = 4U;
    handle->info.flags = HAL_FLASH_FLAG_READ | HAL_FLASH_FLAG_WRITE |
                         HAL_FLASH_FLAG_ERASE;

    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_flash_deinit(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_flash_get_info(hal_flash_t *handle,
                                  hal_flash_info_t *info)
{
    if ((handle == NULL) || (info == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    *info = handle->info;
    return HAL_OK;
}

hal_status_t hal_flash_read(hal_flash_t *handle, uintptr_t address,
                              void *buffer, size_t length)
{
    uintptr_t end;

    if ((handle == NULL) || (buffer == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_OK;
    }

    end = handle->info.start + handle->info.size;
    if ((address < handle->info.start) || (address >= end) ||
        (length > (size_t)(end - address))) {
        return HAL_INVALID_PARAM;
    }

    memcpy(buffer, (const void *)address, length);
    return HAL_OK;
}

hal_status_t hal_flash_write(hal_flash_t *handle, uintptr_t address,
                               const void *buffer, size_t length)
{
    const uint8_t *source = (const uint8_t *)buffer;
    uintptr_t end;
    size_t offset;
    hal_status_t result = HAL_OK;

    if ((handle == NULL) || (buffer == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_OK;
    }

    end = handle->info.start + handle->info.size;
    if ((address < handle->info.start) || (address >= end) ||
        (length > (size_t)(end - address))) {
        return HAL_INVALID_PARAM;
    }
    if ((address % 4U) != 0U) {
        return HAL_INVALID_PARAM;
    }

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return HAL_ERROR;
    }

    for (offset = 0U; offset < length; offset += 4U) {
        uint32_t value = 0xFFFFFFFFU;
        size_t chunk = HAL_MIN((size_t)4U, length - offset);

        memcpy(&value, source + offset, chunk);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              address + offset,
                              (uint64_t)value) != HAL_OK) {
            result = HAL_IO_ERROR;
            break;
        }
    }

    (void)HAL_FLASH_Lock();
    return result;
}

hal_status_t hal_flash_erase(hal_flash_t *handle, uintptr_t address,
                               size_t length)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0U;
    uintptr_t end;
    uint32_t first_sector;
    uint32_t last_sector;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_OK;
    }

    end = handle->info.start + handle->info.size;
    if ((address < handle->info.start) || (address >= end) ||
        (length > (size_t)(end - address))) {
        return HAL_INVALID_PARAM;
    }

    first_sector = f4_flash_sector(address);
    last_sector = f4_flash_sector(address + length - 1U);
    if (address != f4_flash_sector_start(first_sector)) {
        return HAL_INVALID_PARAM;
    }
    if (f4_flash_sector_start(last_sector) >= end) {
        return HAL_INVALID_PARAM;
    }

    memset(&erase, 0, sizeof(erase));
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = first_sector;
    erase.NbSectors = last_sector - first_sector + 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return HAL_ERROR;
    }

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        (void)HAL_FLASH_Lock();
        return HAL_IO_ERROR;
    }

    (void)HAL_FLASH_Lock();
    return HAL_OK;
}

hal_status_t hal_flash_lock(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    (void)HAL_FLASH_Lock();
    return HAL_OK;
}

hal_status_t hal_flash_unlock(hal_flash_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    return f4_hal_status(HAL_FLASH_Unlock());
}

/* ========================================================================== */
/* CRC（软件实现，结果与项目内算法一致）                                      */
/* ========================================================================== */

static uint32_t f4_crc_mask(uint32_t width)
{
    if (width == 8U) {
        return 0x000000FFU;
    }
    if (width == 16U) {
        return 0x0000FFFFU;
    }
    return 0xFFFFFFFFU;
}

static uint32_t f4_crc_reflect(uint32_t value, uint32_t bits)
{
    uint32_t result = 0U;
    uint32_t i;

    for (i = 0U; i < bits; i++) {
        result <<= 1U;
        result |= (value & 1U);
        value >>= 1U;
    }
    return result;
}

hal_status_t hal_crc_init(hal_crc_t *handle,
                            const hal_crc_config_t *config)
{
    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if ((config->width != HAL_CRC_WIDTH_8) &&
        (config->width != HAL_CRC_WIDTH_16) &&
        (config->width != HAL_CRC_WIDTH_32)) {
        return HAL_INVALID_PARAM;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_crc_reset(hal_crc_t *handle)
{
    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_crc_calculate(hal_crc_t *handle, const void *data,
                                 size_t length, uint32_t *result)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t width;
    uint32_t mask;
    uint32_t polynomial;
    uint32_t crc;
    size_t i;
    uint32_t bit;

    if ((handle == NULL) || (result == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if ((length > 0U) && (data == NULL)) {
        return HAL_INVALID_PARAM;
    }

    width = (uint32_t)handle->config.width;
    mask = f4_crc_mask(width);
    polynomial = handle->config.polynomial & mask;
    crc = handle->config.initial_value & mask;

    for (i = 0U; i < length; i++) {
        uint32_t byte = bytes[i];

        if (handle->config.input_reverse) {
            byte = f4_crc_reflect(byte, 8U);
        }

        if (width == 8U) {
            crc ^= byte;
            for (bit = 0U; bit < 8U; bit++) {
                if ((crc & 0x80U) != 0U) {
                    crc = ((crc << 1U) ^ polynomial) & 0xFFU;
                } else {
                    crc = (crc << 1U) & 0xFFU;
                }
            }
        } else if (width == 16U) {
            crc ^= (byte << 8U);
            for (bit = 0U; bit < 8U; bit++) {
                if ((crc & 0x8000U) != 0U) {
                    crc = ((crc << 1U) ^ polynomial) & 0xFFFFU;
                } else {
                    crc = (crc << 1U) & 0xFFFFU;
                }
            }
        } else {
            crc ^= (byte << 24U);
            for (bit = 0U; bit < 8U; bit++) {
                if ((crc & 0x80000000U) != 0U) {
                    crc = (crc << 1U) ^ polynomial;
                } else {
                    crc <<= 1U;
                }
            }
        }
    }

    if (handle->config.output_reverse) {
        crc = f4_crc_reflect(crc, width);
    }
    crc ^= handle->config.xor_output;
    *result = crc & mask;
    return HAL_OK;
}

hal_status_t hal_crc_deinit(hal_crc_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

/* ========================================================================== */
/* RNG（STM32F4 硬件 RNG）                                                    */
/* ========================================================================== */

static RNG_HandleTypeDef s_f4_rng;

hal_status_t hal_rng_init(hal_rng_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    memset(&s_f4_rng, 0, sizeof(s_f4_rng));
    s_f4_rng.Instance = RNG;
    __HAL_RCC_RNG_CLK_ENABLE();

    if (HAL_RNG_Init(&s_f4_rng) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_rng_deinit(hal_rng_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    (void)HAL_RNG_DeInit(&s_f4_rng);
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

hal_status_t hal_rng_get(hal_rng_t *handle, uint32_t *value)
{
    if ((handle == NULL) || (value == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    return f4_hal_status(HAL_RNG_GenerateRandomNumber(&s_f4_rng, value));
}

hal_status_t hal_rng_generate(hal_rng_t *handle, void *buffer,
                                size_t length)
{
    uint8_t *bytes = (uint8_t *)buffer;
    size_t offset = 0U;

    if ((handle == NULL) || (buffer == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }

    while (offset + 4U <= length) {
        uint32_t value;
        if (HAL_RNG_GenerateRandomNumber(&s_f4_rng, &value) != HAL_OK) {
            return HAL_ERROR;
        }
        memcpy(bytes + offset, &value, 4U);
        offset += 4U;
    }

    if (offset < length) {
        uint32_t value;
        size_t remaining = length - offset;
        if (HAL_RNG_GenerateRandomNumber(&s_f4_rng, &value) != HAL_OK) {
            return HAL_ERROR;
        }
        memcpy(bytes + offset, &value, remaining);
    }
    return HAL_OK;
}

/* ========================================================================== */
/* PWR                                                                        */
/* ========================================================================== */

hal_status_t hal_pwr_init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    return HAL_OK;
}

hal_status_t hal_pwr_enter(const hal_pwr_config_t *config)
{
    uint32_t regulator;

    if (config == NULL) {
        return HAL_INVALID_PARAM;
    }

    regulator = config->low_power_regulator ?
                PWR_LOWPOWERREGULATOR_ON : PWR_MAINREGULATOR_ON;

    switch (config->mode) {
    case HAL_PWR_MODE_SLEEP:
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
        break;
    case HAL_PWR_MODE_STOP:
        HAL_PWR_EnterSTOPMode(regulator, PWR_STOPENTRY_WFI);
        break;
    case HAL_PWR_MODE_STANDBY:
        HAL_PWR_EnterSTANDBYMode();
        break;
    default:
        return HAL_INVALID_PARAM;
    }
    return HAL_OK;
}

hal_status_t hal_pwr_get_status(hal_pwr_status_t *status)
{
    if (status == NULL) {
        return HAL_INVALID_PARAM;
    }

    status->reset_flags = 0U;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET) {
        status->reset_flags |= HAL_BIT(0);
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET) {
        status->reset_flags |= HAL_BIT(1);
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET) {
        status->reset_flags |= HAL_BIT(2);
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) {
        status->reset_flags |= HAL_BIT(3);
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET) {
        status->reset_flags |= HAL_BIT(4);
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != RESET) {
        status->reset_flags |= HAL_BIT(5);
    }

    status->wakeup_flags = 0U;
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_WU) != RESET) {
        status->wakeup_flags |= HAL_BIT(0);
    }
#ifdef PWR_FLAG_SB
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_SB) != RESET) {
        status->wakeup_flags |= HAL_BIT(1);
    }
#endif
    return HAL_OK;
}

hal_status_t hal_pwr_clear_flags(void)
{
    __HAL_RCC_CLEAR_RESET_FLAGS();
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
#ifdef PWR_FLAG_SB
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
#endif
    return HAL_OK;
}

/* ========================================================================== */
/* QSPI / SDIO / USB / ETH（STM32F4 不支持）                                  */
/* ========================================================================== */

hal_status_t hal_qspi_init(hal_qspi_t *handle,
                             const hal_qspi_config_t *config)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(config);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_qspi_deinit(hal_qspi_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_qspi_command(hal_qspi_t *handle,
                                const hal_qspi_command_t *command)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(command);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_qspi_transmit(hal_qspi_t *handle, const void *data,
                                 size_t length, uint32_t timeout)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    HAL_UNUSED(timeout);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_qspi_receive(hal_qspi_t *handle, void *data,
                                size_t length, uint32_t timeout)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    HAL_UNUSED(timeout);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_qspi_memory_map(hal_qspi_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_qspi_memory_unmap(hal_qspi_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_sdio_init(hal_sdio_t *handle,
                             const hal_sdio_config_t *config)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(config);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_sdio_deinit(hal_sdio_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_sdio_read(hal_sdio_t *handle, uint32_t block,
                             void *buffer, size_t block_count,
                             uint32_t timeout)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(block);
    HAL_UNUSED(buffer);
    HAL_UNUSED(block_count);
    HAL_UNUSED(timeout);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_sdio_write(hal_sdio_t *handle, uint32_t block,
                              const void *buffer, size_t block_count,
                              uint32_t timeout)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(block);
    HAL_UNUSED(buffer);
    HAL_UNUSED(block_count);
    HAL_UNUSED(timeout);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_sdio_read_dma(hal_sdio_t *handle, uint32_t block,
                                 void *buffer, size_t block_count)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(block);
    HAL_UNUSED(buffer);
    HAL_UNUSED(block_count);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_sdio_write_dma(hal_sdio_t *handle, uint32_t block,
                                  const void *buffer, size_t block_count)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(block);
    HAL_UNUSED(buffer);
    HAL_UNUSED(block_count);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_usb_init(hal_usb_t *handle,
                            const hal_usb_config_t *config)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(config);
    /* 简化实现：F4 USB PCD 由用户工程自行初始化。 */
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_usb_deinit(hal_usb_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_usb_start(hal_usb_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_usb_stop(hal_usb_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_usb_connect(hal_usb_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_usb_disconnect(hal_usb_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_eth_init(hal_eth_t *handle,
                            const hal_eth_config_t *config)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(config);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_eth_deinit(hal_eth_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_eth_start(hal_eth_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_eth_stop(hal_eth_t *handle)
{
    HAL_UNUSED(handle);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_eth_transmit(hal_eth_t *handle, const void *data,
                                size_t length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_eth_receive(hal_eth_t *handle, void *buffer,
                               size_t buffer_size, size_t *received_length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(buffer);
    HAL_UNUSED(buffer_size);
    HAL_UNUSED(received_length);
    return HAL_NOT_SUPPORTED;
}

/* ========================================================================== */
/* CLOCK                                                                      */
/* ========================================================================== */

static uint32_t f4_apb_divider(uint32_t hclk, uint32_t max_clock)
{
    uint32_t divider = 1U;

    while (((hclk / divider) > max_clock) && (divider < 16U)) {
        divider <<= 1;
    }

    switch (divider) {
    case 2U:
        return RCC_HCLK_DIV2;
    case 4U:
        return RCC_HCLK_DIV4;
    case 8U:
        return RCC_HCLK_DIV8;
    case 16U:
        return RCC_HCLK_DIV16;
    case 1U:
    default:
        return RCC_HCLK_DIV1;
    }
}

hal_status_t hal_clock_init(const hal_clock_config_t *config)
{
    RCC_OscInitTypeDef oscillator;
    RCC_ClkInitTypeDef clock;
    uint32_t target;
    uint32_t source;
    uint32_t latency;

    if (config == NULL) {
        return HAL_INVALID_PARAM;
    }

    memset(&oscillator, 0, sizeof(oscillator));
    memset(&clock, 0, sizeof(clock));

    switch (config->source) {
    case HAL_CLOCK_HSI:
        oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        oscillator.HSIState = RCC_HSI_ON;
        oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
            return HAL_ERROR;
        }
        target = HSI_VALUE;
        source = RCC_SYSCLKSOURCE_HSI;
        break;

    case HAL_CLOCK_HSE:
        oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
        oscillator.HSEState = RCC_HSE_ON;
        if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
            return HAL_ERROR;
        }
        target = HSE_VALUE;
        source = RCC_SYSCLKSOURCE_HSE;
        break;

    case HAL_CLOCK_PLL:
        if (HSE_VALUE == 0U) {
            return HAL_NOT_SUPPORTED;
        }
        target = (config->pll_frequency != 0U) ? config->pll_frequency :
                                                 config->frequency;
        if (target == 0U) {
            target = 168000000U;
        }
        {
            /* 固定 VCO 输入 1MHz，P=2，Q=7（USB 48MHz）。 */
            uint32_t pll_m = HSE_VALUE / 1000000U;
            uint32_t pll_n;

            if (pll_m < 2U) {
                pll_m = 2U;
            }
            if (pll_m > 63U) {
                pll_m = 63U;
            }

            pll_n = (target * 2U) / 1000000U;
            if (pll_n < 50U) {
                pll_n = 50U;
            }
            if (pll_n > 432U) {
                pll_n = 432U;
            }

            oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
            oscillator.HSEState = RCC_HSE_ON;
            oscillator.PLL.PLLState = RCC_PLL_ON;
            oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
            oscillator.PLL.PLLM = pll_m;
            oscillator.PLL.PLLN = pll_n;
            oscillator.PLL.PLLP = RCC_PLLP_DIV2;
            oscillator.PLL.PLLQ = RCC_PLLQ_DIV7;
        }
        if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
            return HAL_ERROR;
        }
        source = RCC_SYSCLKSOURCE_PLLCLK;
        break;

    case HAL_CLOCK_LSI:
        oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSI;
        oscillator.LSIState = RCC_LSI_ON;
        return f4_hal_status(HAL_RCC_OscConfig(&oscillator));

    case HAL_CLOCK_LSE:
        oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSE;
        oscillator.LSEState = RCC_LSE_ON;
        return f4_hal_status(HAL_RCC_OscConfig(&oscillator));

    default:
        return HAL_INVALID_PARAM;
    }

    /* 3.3V 供电：每 30MHz 需要 1 个等待周期。 */
    if (target <= 30000000U) {
        latency = FLASH_LATENCY_0;
    } else {
        uint32_t wait_states = (target - 1U) / 30000000U;
        if (wait_states > 15U) {
            wait_states = 15U;
        }
        latency = wait_states;
    }

    clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = source;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = f4_apb_divider(target, 42000000U);
    clock.APB2CLKDivider = f4_apb_divider(target, 84000000U);

    return f4_hal_status(HAL_RCC_ClockConfig(&clock, latency));
}

uint32_t hal_clock_get_frequency(hal_clock_bus_t bus)
{
    switch (bus) {
    case HAL_CLOCK_CORE:
    case HAL_CLOCK_AHB:
        return HAL_RCC_GetHCLKFreq();
    case HAL_CLOCK_APB1:
        return HAL_RCC_GetPCLK1Freq();
    case HAL_CLOCK_APB2:
        return HAL_RCC_GetPCLK2Freq();
    case HAL_CLOCK_APB3:
    case HAL_CLOCK_APB4:
    default:
        return 0U;
    }
}

hal_status_t hal_clock_enable(void *peripheral)
{
    if (peripheral == NULL) {
        return HAL_INVALID_PARAM;
    }
    /* 外设时钟需由具体驱动通过 __HAL_RCC_xxx_CLK_ENABLE() 打开。 */
    return HAL_NOT_SUPPORTED;
}

hal_status_t hal_clock_disable(void *peripheral)
{
    if (peripheral == NULL) {
        return HAL_INVALID_PARAM;
    }
    return HAL_NOT_SUPPORTED;
}

/* ========================================================================== */
/* CORE / PLATFORM                                                            */
/* ========================================================================== */

uint32_t hal_get_capabilities(hal_device_type_t type, uint8_t instance)
{
    HAL_UNUSED(instance);

    switch (type) {
    case HAL_DEVICE_GPIO:
        return 0U;
    case HAL_DEVICE_UART:
        return HAL_CAP_DMA | HAL_CAP_IT;
    case HAL_DEVICE_SPI:
        return HAL_CAP_DMA;
    case HAL_DEVICE_I2C:
        return HAL_CAP_DMA;
    case HAL_DEVICE_CAN:
        return HAL_CAP_IT;
    case HAL_DEVICE_ADC:
        return HAL_CAP_DMA | HAL_CAP_IT;
    case HAL_DEVICE_DAC:
        return HAL_CAP_DMA;
    case HAL_DEVICE_PWM:
        return 0U;
    case HAL_DEVICE_TIMER:
        return HAL_CAP_IT;
    case HAL_DEVICE_FLASH:
        return HAL_FLASH_FLAG_READ | HAL_FLASH_FLAG_WRITE |
               HAL_FLASH_FLAG_ERASE;
    case HAL_DEVICE_USB:
    case HAL_DEVICE_ETH:
    default:
        return 0U;
    }
}

hal_status_t hal_init(void)
{
    return HAL_OK;
}

hal_status_t hal_deinit(void)
{
    return HAL_OK;
}

hal_status_t hal_get_info(hal_mcu_info_t *info)
{
    if (info == NULL) {
        return HAL_INVALID_PARAM;
    }

    memset(info, 0, sizeof(*info));
    info->family = HAL_MCU_STM32F4;
    info->core_clock = 168000000U;
    info->ahb_clock = 168000000U;
    info->apb1_clock = 42000000U;
    info->apb2_clock = 84000000U;
    info->flash_size = IOT_F4_FLASH_SIZE;
    info->sram_size = 192U * 1024U;
    return HAL_OK;
}

#endif /* IOT_STM32_F4 */
