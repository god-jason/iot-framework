/*
 * hal_wdg.c - STM32H7 WDG HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_H7)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

static uint32_t stm32_iwdg_prescaler(uint32_t divider)
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

static uint32_t stm32_wwdg_prescaler(uint32_t divider)
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

int hal_wdg_init(hal_wdg_t *handle,
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
        hiwdg->Init.Prescaler = stm32_iwdg_prescaler(divider);
        hiwdg->Init.Reload = reload;

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
        hwwdg->Init.Prescaler = stm32_wwdg_prescaler(divider);
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

int hal_wdg_start(hal_wdg_t *handle)
{
    if ((handle == NULL) || (handle->base.state == HAL_STATE_RESET)) {
        return HAL_NOT_READY;
    }
    /* IWDG/WWDG 在 HAL_xxx_Init() 时已启动。 */
    return HAL_OK;
}

int hal_wdg_refresh(hal_wdg_t *handle)
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
        return ros_cube_status(HAL_IWDG_Refresh(hiwdg));
    } else {
        WWDG_HandleTypeDef *hwwdg =
            (WWDG_HandleTypeDef *)handle->base.instance;
        if (hwwdg == NULL) {
            return HAL_NOT_READY;
        }
        return ros_cube_status(HAL_WWDG_Refresh(hwwdg));
    }
}

int hal_wdg_stop(hal_wdg_t *handle)
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


#endif /* ROS_STM32_H7 */
