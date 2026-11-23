/*
 * hal_i2c.c - STM32F4 I2C HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

static uint16_t stm32_i2c_dev_address(hal_i2c_t *handle, uint16_t address)
{
    if ((handle != NULL) &&
        (handle->config.address_mode == HAL_I2C_ADDR_10BIT)) {
        return address;
    }
    return (uint16_t)(address << 1);
}

int hal_i2c_init(hal_i2c_t *handle,
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

int hal_i2c_deinit(hal_i2c_t *handle)
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

int hal_i2c_master_transmit(hal_i2c_t *handle,
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
    status = HAL_I2C_Master_Transmit(hi2c, stm32_i2c_dev_address(handle, address),
                                     (uint8_t *)data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return ros_cube_status(status);
}

int hal_i2c_master_receive(hal_i2c_t *handle,
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
    status = HAL_I2C_Master_Receive(hi2c, stm32_i2c_dev_address(handle, address),
                                    data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return ros_cube_status(status);
}

int hal_i2c_master_transfer(hal_i2c_t *handle,
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
        status = HAL_I2C_Mem_Read(hi2c, stm32_i2c_dev_address(handle, address),
                                  mem_address, mem_size, rx_data,
                                  (uint16_t)rx_length, timeout);
        handle->base.state = (status == HAL_OK) ? HAL_STATE_READY :
                                                 HAL_STATE_ERROR;
        return ros_cube_status(status);
    }

    if (tx_length > 0U) {
        handle->base.state = HAL_STATE_BUSY_TX;
        status = HAL_I2C_Master_Transmit(hi2c,
                                         stm32_i2c_dev_address(handle, address),
                                         (uint8_t *)tx_data, (uint16_t)tx_length,
                                         timeout);
        if (status != HAL_OK) {
            handle->base.state = HAL_STATE_ERROR;
            return ros_cube_status(status);
        }
    }

    if (rx_length > 0U) {
        handle->base.state = HAL_STATE_BUSY_RX;
        status = HAL_I2C_Master_Receive(hi2c,
                                        stm32_i2c_dev_address(handle, address),
                                        rx_data, (uint16_t)rx_length, timeout);
        if (status != HAL_OK) {
            handle->base.state = HAL_STATE_ERROR;
            return ros_cube_status(status);
        }
    }

    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_i2c_mem_write(hal_i2c_t *handle, uint16_t address,
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
    status = HAL_I2C_Mem_Write(hi2c, stm32_i2c_dev_address(handle, address), reg,
                               mem_size, (uint8_t *)data, (uint16_t)length,
                               timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return ros_cube_status(status);
}

int hal_i2c_mem_read(hal_i2c_t *handle, uint16_t address,
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
    status = HAL_I2C_Mem_Read(hi2c, stm32_i2c_dev_address(handle, address), reg,
                              mem_size, data, (uint16_t)length, timeout);
    handle->base.state = (status == HAL_OK) ? HAL_STATE_READY : HAL_STATE_ERROR;
    return ros_cube_status(status);
}

int hal_i2c_master_transmit_dma(hal_i2c_t *handle,
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
                                         stm32_i2c_dev_address(handle, address),
                                         (uint8_t *)data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_TX;
    }
    return ros_cube_status(status);
}

int hal_i2c_master_receive_dma(hal_i2c_t *handle,
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
                                        stm32_i2c_dev_address(handle, address),
                                        data, (uint16_t)length);
    if (status == HAL_OK) {
        handle->base.state = HAL_STATE_BUSY_RX;
    }
    return ros_cube_status(status);
}

int hal_i2c_is_device_ready(hal_i2c_t *handle,
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
    return ros_cube_status(HAL_I2C_IsDeviceReady(
        hi2c, stm32_i2c_dev_address(handle, address), trials, timeout));
}

int hal_i2c_bus_recover(hal_i2c_t *handle)
{
    HAL_UNUSED(handle);
    /* 需要用户提供 SCL/SDA 引脚信息才能做时钟翻转恢复。 */
    return HAL_NOT_SUPPORTED;
}

int hal_i2c_abort(hal_i2c_t *handle)
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
    return ros_cube_status(HAL_I2C_Init(hi2c));
}

hal_state_e hal_i2c_get_state(hal_i2c_t *handle)
{
    return (handle != NULL) ? handle->base.state : HAL_STATE_RESET;
}


#endif /* ROS_STM32_F4 */
