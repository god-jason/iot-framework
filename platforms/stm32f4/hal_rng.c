/*
 * hal_rng.c - STM32F4 RNG HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

static RNG_HandleTypeDef s_stm32_rng;

int hal_rng_init(hal_rng_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state != HAL_STATE_RESET) {
        return HAL_ALREADY_INIT;
    }

    memset(&s_stm32_rng, 0, sizeof(s_stm32_rng));
    s_stm32_rng.Instance = RNG;
    __HAL_RCC_RNG_CLK_ENABLE();

    if (HAL_RNG_Init(&s_stm32_rng) != HAL_OK) {
        handle->base.state = HAL_STATE_ERROR;
        return HAL_ERROR;
    }

    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_rng_deinit(hal_rng_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    (void)HAL_RNG_DeInit(&s_stm32_rng);
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_rng_get(hal_rng_t *handle, uint32_t *value)
{
    if ((handle == NULL) || (value == NULL)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    return ros_cube_status(HAL_RNG_GenerateRandomNumber(&s_stm32_rng, value));
}

int hal_rng_generate(hal_rng_t *handle, void *buffer,
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
        if (HAL_RNG_GenerateRandomNumber(&s_stm32_rng, &value) != HAL_OK) {
            return HAL_ERROR;
        }
        memcpy(bytes + offset, &value, 4U);
        offset += 4U;
    }

    if (offset < length) {
        uint32_t value;
        size_t remaining = length - offset;
        if (HAL_RNG_GenerateRandomNumber(&s_stm32_rng, &value) != HAL_OK) {
            return HAL_ERROR;
        }
        memcpy(bytes + offset, &value, remaining);
    }
    return HAL_OK;
}


#endif /* ROS_STM32_F4 */
