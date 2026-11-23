/*
 * hal_pwr.c - STM32F4 PWR HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

int hal_pwr_init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    return HAL_OK;
}

int hal_pwr_enter(const hal_pwr_config_t *config)
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

int hal_pwr_get_status(hal_pwr_status_t *status)
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

int hal_pwr_clear_flags(void)
{
    __HAL_RCC_CLEAR_RESET_FLAGS();
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
#ifdef PWR_FLAG_SB
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
#endif
    return HAL_OK;
}


#endif /* ROS_STM32_F4 */
