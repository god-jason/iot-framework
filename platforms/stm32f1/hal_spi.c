/*
 * hal_spi.c - STM32F1 SPI 寄存器实现（不依赖 Cube HAL）
 *
 * 支持 SPI1/SPI2（大容量型号含 SPI3）主机/从机、8/16 位、四种
 * CPOL/CPHA 组合与 LSB/MSB 位序。片选与引脚复用由板级/驱动配置。
 *
 * 编号：config.id = 0/1/2 依次对应 SPI1/SPI2/SPI3（按芯片裁剪）。
 */

#if defined(ROS_STM32_F1)

#include "hal.h"
#include "stm32f10x.h"

#define STM32_SPI_SR_RXNE   (1UL << 0)
#define STM32_SPI_SR_TXE    (1UL << 1)
#define STM32_SPI_SR_OVR    (1UL << 6)
#define STM32_SPI_SR_BSY    (1UL << 7)

#define STM32_SPI_CR1_CPHA  (1UL << 0)
#define STM32_SPI_CR1_CPOL  (1UL << 1)
#define STM32_SPI_CR1_MSTR  (1UL << 2)
#define STM32_SPI_CR1_SPE   (1UL << 6)
#define STM32_SPI_CR1_LSBF  (1UL << 7)
#define STM32_SPI_CR1_SSI   (1UL << 8)
#define STM32_SPI_CR1_SSM   (1UL << 9)
#define STM32_SPI_CR1_RXONLY (1UL << 10)
#define STM32_SPI_CR1_DFF   (1UL << 11)
#define STM32_SPI_CR1_BIDIOE (1UL << 14)
#define STM32_SPI_CR1_BIDIMODE (1UL << 15)

static SPI_TypeDef *const s_stm32_spi_inst[] = {
    SPI1,
#ifdef SPI2
    SPI2,
#endif
#ifdef SPI3
    SPI3,
#endif
};

#define STM32_SPI_CH_COUNT \
    (sizeof(s_stm32_spi_inst) / sizeof(s_stm32_spi_inst[0]))

static uint32_t stm32_spi_pclk2(void)
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

static uint32_t stm32_spi_pclk1(void)
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

static SPI_TypeDef *stm32_spi_instance(hal_spi_t *handle)
{
    return (SPI_TypeDef *)handle->base.instance;
}

static uint32_t stm32_spi_prescaler_bits(uint32_t pclk, uint32_t baudrate)
{
    uint32_t i;

    if (baudrate == 0U) {
        return 0U;
    }
    for (i = 0U; i < 8U; i++) {
        if ((pclk >> (i + 1U)) <= baudrate) {
            return i;
        }
    }
    return 7U;
}

static int stm32_spi_wait(SPI_TypeDef *spi, uint32_t mask,
                          bool expect_set, uint32_t timeout)
{
    uint32_t start = hal_tick_get();

    while ((((spi->SR & mask) != 0U) != expect_set)) {
        if (timeout != HAL_TIMEOUT_FOREVER &&
            ((uint32_t)(hal_tick_get() - start) >= timeout)) {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

int hal_spi_init(hal_spi_t *handle, const hal_spi_config_t *config)
{
    SPI_TypeDef *spi;
    uint32_t cr1 = 0U;
    uint32_t pclk;

    if ((handle == NULL) || (config == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }
    if ((config->data_bits != 8U) && (config->data_bits != 16U)) {
        return HAL_NOT_SUPPORTED;
    }

    if (config->id >= STM32_SPI_CH_COUNT) {
        return HAL_INVALID_PARAM;
    }
    spi = s_stm32_spi_inst[config->id];
    handle->base.instance = spi;

    if (spi == SPI1) {
        RCC->APB2ENR |= (1UL << 12);
        pclk = stm32_spi_pclk2();
    }
#ifdef SPI2
    else if (spi == SPI2) {
        RCC->APB1ENR |= (1UL << 14);
        pclk = stm32_spi_pclk1();
    }
#endif
#ifdef SPI3
    else if (spi == SPI3) {
        RCC->APB1ENR |= (1UL << 15);
        pclk = stm32_spi_pclk1();
    }
#endif
    else {
        return HAL_INVALID_PARAM;
    }

    spi->CR1 = 0U;
    spi->CR2 = 0U;

    cr1 = STM32_SPI_CR1_SSM | STM32_SPI_CR1_SSI;
    if (config->mode == HAL_SPI_MODE_MASTER) {
        cr1 |= STM32_SPI_CR1_MSTR;
    }
    if (config->cpol == HAL_SPI_CPOL_HIGH) {
        cr1 |= STM32_SPI_CR1_CPOL;
    }
    if (config->cpha == HAL_SPI_CPHA_2EDGE) {
        cr1 |= STM32_SPI_CR1_CPHA;
    }
    if (config->bit_order == HAL_SPI_BITORDER_LSB) {
        cr1 |= STM32_SPI_CR1_LSBF;
    }
    if (config->data_bits == 16U) {
        cr1 |= STM32_SPI_CR1_DFF;
    }

    switch (config->direction) {
    case HAL_SPI_DIRECTION_2LINES:
        break;
    case HAL_SPI_DIRECTION_2LINES_RXONLY:
        cr1 |= STM32_SPI_CR1_RXONLY;
        break;
    case HAL_SPI_DIRECTION_1LINE:
        cr1 |= STM32_SPI_CR1_BIDIMODE | STM32_SPI_CR1_BIDIOE;
        break;
    default:
        return HAL_INVALID_PARAM;
    }

    cr1 |= (stm32_spi_prescaler_bits(pclk, config->baudrate) << 3);
    spi->CR1 = cr1 | STM32_SPI_CR1_SPE;

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_spi_deinit(hal_spi_t *handle)
{
    SPI_TypeDef *spi;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    spi = stm32_spi_instance(handle);
    if (spi != NULL) {
        spi->CR1 &= ~STM32_SPI_CR1_SPE;
        spi->CR1 = 0U;
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_spi_transmit(hal_spi_t *handle, const void *data,
                     size_t length, uint32_t timeout)
{
    SPI_TypeDef *spi;
    size_t i;
    int result;

    if ((handle == NULL) || (data == NULL) || (length == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    spi = stm32_spi_instance(handle);
    if (spi == NULL) {
        return HAL_NOT_READY;
    }

    if (handle->config.direction == HAL_SPI_DIRECTION_1LINE) {
        spi->CR1 |= STM32_SPI_CR1_BIDIOE;
    }

    handle->base.state = HAL_STATE_BUSY_TX;
    for (i = 0U; i < length; i++) {
        uint16_t value = (handle->config.data_bits == 16U) ?
                         ((const uint16_t *)data)[i] :
                         (uint16_t)((const uint8_t *)data)[i];

        result = stm32_spi_wait(spi, STM32_SPI_SR_TXE, true, timeout);
        if (result != HAL_OK) {
            handle->base.state = HAL_STATE_ERROR;
            return result;
        }
        spi->DR = value;
    }
    result = stm32_spi_wait(spi, STM32_SPI_SR_BSY, false, timeout);
    if (result != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return result;
    }
    /* 清空可能残留的接收数据，避免 OVR 影响后续传输 */
    if ((spi->SR & (STM32_SPI_SR_RXNE | STM32_SPI_SR_OVR)) != 0U) {
        (void)spi->DR;
        (void)spi->SR;
    }
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_spi_receive(hal_spi_t *handle, void *data,
                    size_t length, uint32_t timeout)
{
    SPI_TypeDef *spi;
    size_t i;
    int result;

    if ((handle == NULL) || (data == NULL) || (length == 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    spi = stm32_spi_instance(handle);
    if (spi == NULL) {
        return HAL_NOT_READY;
    }

    if (handle->config.direction == HAL_SPI_DIRECTION_1LINE) {
        spi->CR1 &= ~STM32_SPI_CR1_BIDIOE;
    }

    handle->base.state = HAL_STATE_BUSY_RX;
    for (i = 0U; i < length; i++) {
        uint16_t value;

        /* 仅接收模式下读 DR 即产生时钟，无需写哑数据。 */
        if (handle->config.direction != HAL_SPI_DIRECTION_2LINES_RXONLY) {
            spi->DR = 0xFFFFU;
        }
        result = stm32_spi_wait(spi, STM32_SPI_SR_RXNE, true, timeout);
        if (result != HAL_OK) {
            handle->base.state = HAL_STATE_ERROR;
            return result;
        }
        value = (uint16_t)spi->DR;
        if (handle->config.data_bits == 16U) {
            ((uint16_t *)data)[i] = value;
        } else {
            ((uint8_t *)data)[i] = (uint8_t)value;
        }
    }
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_spi_transfer(hal_spi_t *handle, const void *tx_data,
                     void *rx_data, size_t length, uint32_t timeout)
{
    SPI_TypeDef *spi;
    size_t i;
    int result;

    if ((handle == NULL) || (length == 0U) ||
        ((tx_data == NULL) && (rx_data == NULL))) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    spi = stm32_spi_instance(handle);
    if (spi == NULL) {
        return HAL_NOT_READY;
    }

    handle->base.state = HAL_STATE_BUSY_TX_RX;
    for (i = 0U; i < length; i++) {
        uint16_t tx = 0xFFFFU;
        uint16_t rx;

        if (tx_data != NULL) {
            tx = (handle->config.data_bits == 16U) ?
                 ((const uint16_t *)tx_data)[i] :
                 (uint16_t)((const uint8_t *)tx_data)[i];
        }
        result = stm32_spi_wait(spi, STM32_SPI_SR_TXE, true, timeout);
        if (result != HAL_OK) {
            handle->base.state = HAL_STATE_ERROR;
            return result;
        }
        spi->DR = tx;
        result = stm32_spi_wait(spi, STM32_SPI_SR_RXNE, true, timeout);
        if (result != HAL_OK) {
            handle->base.state = HAL_STATE_ERROR;
            return result;
        }
        rx = (uint16_t)spi->DR;
        if (rx_data != NULL) {
            if (handle->config.data_bits == 16U) {
                ((uint16_t *)rx_data)[i] = rx;
            } else {
                ((uint8_t *)rx_data)[i] = (uint8_t)rx;
            }
        }
    }
    result = stm32_spi_wait(spi, STM32_SPI_SR_BSY, false, timeout);
    if (result != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return result;
    }
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_spi_transmit_dma(hal_spi_t *handle, const void *data, size_t length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    return HAL_NOT_SUPPORTED;
}

int hal_spi_receive_dma(hal_spi_t *handle, void *data, size_t length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(data);
    HAL_UNUSED(length);
    return HAL_NOT_SUPPORTED;
}

int hal_spi_transfer_dma(hal_spi_t *handle, const void *tx_data,
                         void *rx_data, size_t length)
{
    HAL_UNUSED(handle);
    HAL_UNUSED(tx_data);
    HAL_UNUSED(rx_data);
    HAL_UNUSED(length);
    return HAL_NOT_SUPPORTED;
}

int hal_spi_set_speed(hal_spi_t *handle, uint32_t baudrate)
{
    SPI_TypeDef *spi;
    uint32_t pclk;
    uint32_t bits;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (handle->config.mode != HAL_SPI_MODE_MASTER) {
        return HAL_NOT_SUPPORTED;
    }
    spi = stm32_spi_instance(handle);
    if (spi == NULL) {
        return HAL_NOT_READY;
    }

    pclk = (spi == SPI1) ? stm32_spi_pclk2() : stm32_spi_pclk1();
    bits = stm32_spi_prescaler_bits(pclk, baudrate) << 3;

    spi->CR1 &= ~STM32_SPI_CR1_SPE;
    spi->CR1 = (spi->CR1 & ~(0x7UL << 3)) | bits;
    spi->CR1 |= STM32_SPI_CR1_SPE;
    handle->config.baudrate = baudrate;
    return HAL_OK;
}

int hal_spi_abort(hal_spi_t *handle)
{
    SPI_TypeDef *spi;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    spi = stm32_spi_instance(handle);
    if (spi == NULL) {
        return HAL_NOT_READY;
    }
    spi->CR1 &= ~STM32_SPI_CR1_SPE;
    spi->CR1 |= STM32_SPI_CR1_SPE;
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

hal_state_e hal_spi_get_state(hal_spi_t *handle)
{
    if (handle == NULL) {
        return HAL_STATE_RESET;
    }
    return handle->base.state;
}

#endif /* ROS_STM32_F1 */
