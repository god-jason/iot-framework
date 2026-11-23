/*
 * hal_clock.c - STM32F4 CLOCK HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_F4)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

static uint32_t stm32_apb_divider(uint32_t hclk, uint32_t max_clock)
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

int hal_clock_init(const hal_clock_config_t *config)
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
            oscillator.PLL.PLLQ = 7U;   /* F4 HAL 的 PLLQ 直接写 2..15 */
        }
        if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
            return HAL_ERROR;
        }
        source = RCC_SYSCLKSOURCE_PLLCLK;
        break;

    case HAL_CLOCK_LSI:
        oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSI;
        oscillator.LSIState = RCC_LSI_ON;
        return ros_cube_status(HAL_RCC_OscConfig(&oscillator));

    case HAL_CLOCK_LSE:
        oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSE;
        oscillator.LSEState = RCC_LSE_ON;
        return ros_cube_status(HAL_RCC_OscConfig(&oscillator));

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
    clock.APB1CLKDivider = stm32_apb_divider(target, 42000000U);
    clock.APB2CLKDivider = stm32_apb_divider(target, 84000000U);

    return ros_cube_status(HAL_RCC_ClockConfig(&clock, latency));
}

uint32_t hal_clock_get_frequency(hal_clock_bus_e bus)
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

int hal_clock_enable(void *peripheral)
{
    if (peripheral == NULL) {
        return HAL_INVALID_PARAM;
    }
    /* 外设时钟需由具体驱动通过 __HAL_RCC_xxx_CLK_ENABLE() 打开。 */
    return HAL_NOT_SUPPORTED;
}

int hal_clock_disable(void *peripheral)
{
    if (peripheral == NULL) {
        return HAL_INVALID_PARAM;
    }
    return HAL_NOT_SUPPORTED;
}


#endif /* ROS_STM32_F4 */
