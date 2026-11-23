/*
 * hal_spi.c - STM32H7 SPI HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 *
 * 编号：config.id = 0..N 依次对应 SPI1..SPI6；
 * hal_spi_init() 内部按编号取静态 Cube 句柄，调用者无需提供实例。
 */

#if defined(ROS_STM32_H7)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

#define STM32_SPI_COUNT    6U

static SPI_HandleTypeDef s_hspi[STM32_SPI_COUNT];

static SPI_TypeDef *const s_spi_inst[STM32_SPI_COUNT] = {
    [0] = SPI1,
#ifdef SPI2
    [1] = SPI2,
#endif
#ifdef SPI3
    [2] = SPI3,
#endif
#ifdef SPI4
    [3] = SPI4,
#endif
#ifdef SPI5
    [4] = SPI5,
#endif
#ifdef SPI6
    [5] = SPI6,
#endif
};

static SPI_HandleTypeDef *stm32_spi_instance(uint8_t instance)
{
    SPI_HandleTypeDef *hspi;

    if ((instance >= STM32_SPI_COUNT) || (s_spi_inst[instance] == NULL)) {
        return NULL;
    }
    hspi = &s_hspi[instance];
    memset(hspi, 0, sizeof(*hspi));
    hspi->Instance = s_spi_inst[instance];
    return hspi;
}

static uint32_t stm32_spi_prescaler(SPI_TypeDef *instance, uint32_t baudrate)
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

int hal_spi_init(hal_spi_t *handle,
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

    hspi = stm32_spi_instance(config->id);
    if (hspi == NULL) {
        return HAL_INVALID_PARAM;
    }
    handle->base.instance = hspi;

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
    hspi->Init.BaudRatePrescaler = stm32_spi_prescaler(hspi->Instance,
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

int hal_spi_deinit(hal_spi_t *handle)
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

int hal_spi_transmit(hal_spi_t *handle, const void *data,
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
    return ros_cube_status(status);
}

int hal_spi_receive(hal_spi_t *handle, void *data,
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
    return ros_cube_status(status);
}

int hal_spi_transfer(hal_spi_t *handle, const void *tx_data,
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
    return ros_cube_status(status);
}

int hal_spi_transmit_dma(hal_spi_t *handle,
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
    return ros_cube_status(status);
}

int hal_spi_receive_dma(hal_spi_t *handle, void *data,
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
    return ros_cube_status(status);
}

int hal_spi_transfer_dma(hal_spi_t *handle,
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
    return ros_cube_status(status);
}

int hal_spi_set_speed(hal_spi_t *handle, uint32_t baudrate)
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

    hspi->Init.BaudRatePrescaler = stm32_spi_prescaler(hspi->Instance, baudrate);
    if (HAL_SPI_Init(hspi) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }
    handle->config.baudrate = baudrate;
    return HAL_OK;
}

int hal_spi_abort(hal_spi_t *handle)
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
    return ros_cube_status(HAL_SPI_Init(hspi));
}

hal_state_e hal_spi_get_state(hal_spi_t *handle)
{
    return (handle != NULL) ? handle->base.state : HAL_STATE_RESET;
}


#endif /* ROS_STM32_H7 */
