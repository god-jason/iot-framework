/*
 * hal_crc.c - STM32F1 CRC（软件实现，可按任意多项式/位宽计算）
 *
 * F1 的硬件 CRC 仅支持固定多项式与 32 位数据，无法覆盖公共接口的
 * 参数化需求，因此这里用查表无关的逐位算法实现，行为与其他系列一致。
 */

#if defined(ROS_STM32_F1)

#include "hal.h"

static uint32_t stm32_crc_mask(uint32_t width)
{
    if (width == 8U) {
        return 0x000000FFU;
    }
    if (width == 16U) {
        return 0x0000FFFFU;
    }
    return 0xFFFFFFFFU;
}

static uint32_t stm32_crc_reflect(uint32_t value, uint32_t bits)
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

int hal_crc_init(hal_crc_t *handle, const hal_crc_config_t *config)
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

int hal_crc_reset(hal_crc_t *handle)
{
    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_crc_calculate(hal_crc_t *handle, const void *data,
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
    mask = stm32_crc_mask(width);
    polynomial = handle->config.polynomial & mask;
    crc = handle->config.initial_value & mask;

    for (i = 0U; i < length; i++) {
        uint32_t byte = bytes[i];

        if (handle->config.input_reverse) {
            byte = stm32_crc_reflect(byte, 8U);
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
        crc = stm32_crc_reflect(crc, width);
    }
    crc ^= handle->config.xor_output;
    *result = crc & mask;
    return HAL_OK;
}

int hal_crc_deinit(hal_crc_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

#endif /* ROS_STM32_F1 */
