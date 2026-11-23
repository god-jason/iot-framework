/*
 * hal_clock.c - STM32H7 CLOCK HAL 适配（HAL2 骨架）
 *
 * 只覆盖 HSI/HSE/LSI/LSE 以及基于 HSE 的 PLL1 配置；VOS 电压等级默认按
 * LDO 供电、VOS0 处理，SMPS/外部供电的板子需在 board_init() 中自行配置
 * 电源，或在编译选项中覆盖 ROS_H7_PWR_SUPPLY。
 */

#if defined(ROS_STM32_H7)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

#ifndef ROS_H7_PWR_SUPPLY
#define ROS_H7_PWR_SUPPLY PWR_LDO_SUPPLY
#endif

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

/* 由 VCO 输入频率选择 H7 的 PLL 输入范围档位。 */
static uint32_t stm32_pll_vci_range(uint32_t vco_in)
{
    if (vco_in <= 2000000U) {
        return RCC_PLL1VCIRANGE_0;
    }
    if (vco_in <= 4000000U) {
        return RCC_PLL1VCIRANGE_1;
    }
    if (vco_in <= 8000000U) {
        return RCC_PLL1VCIRANGE_2;
    }
    return RCC_PLL1VCIRANGE_3;
}

static int stm32_clock_pll(RCC_OscInitTypeDef *oscillator,
                           uint32_t target, uint32_t *source)
{
    uint32_t pll_m;
    uint32_t vco_in;
    uint32_t pll_n;
    uint32_t vco_out;

    if (HSE_VALUE == 0U) {
        return HAL_NOT_SUPPORTED;
    }

    /* 以 2MHz 左右的 VCO 输入计算 N，VCO 输出限制在宽范围 192~836MHz。 */
    pll_m = HSE_VALUE / 2000000U;
    if (pll_m < 1U) {
        pll_m = 1U;
    }
    if (pll_m > 63U) {
        pll_m = 63U;
    }
    vco_in = HSE_VALUE / pll_m;
    if (vco_in == 0U) {
        return HAL_NOT_SUPPORTED;
    }

    pll_n = (target * 2U) / vco_in;
    if (pll_n < 4U) {
        pll_n = 4U;
    }
    if (pll_n > 512U) {
        pll_n = 512U;
    }
    vco_out = vco_in * pll_n;

    oscillator->OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator->HSEState = RCC_HSE_ON;
    oscillator->PLL.PLLState = RCC_PLL_ON;
    oscillator->PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator->PLL.PLLM = pll_m;
    oscillator->PLL.PLLN = pll_n;
    oscillator->PLL.PLLP = 2U;              /* VCO/2 -> 系统时钟 */
    oscillator->PLL.PLLQ = 10U;             /* 480MHz/10 = 48MHz（USB） */
    oscillator->PLL.PLLR = 2U;
    oscillator->PLL.PLLRGE = stm32_pll_vci_range(vco_in);
    oscillator->PLL.PLLVCOSEL = (vco_out >= 192000000U) ?
                                RCC_PLL1VCOWIDE : RCC_PLL1VCOMEDIUM;
    *source = RCC_SYSCLKSOURCE_PLLCLK;
    return HAL_OK;
}

int hal_clock_init(const hal_clock_config_t *config)
{
    RCC_OscInitTypeDef oscillator;
    RCC_ClkInitTypeDef clock;
    uint32_t target;
    uint32_t source;
    uint32_t wait_states;
    int result;

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
        oscillator.HSIDiv = RCC_HSI_DIV1;
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
        target = (config->pll_frequency != 0U) ? config->pll_frequency :
                                                 config->frequency;
        if (target == 0U) {
            target = 480000000U;
        }
        result = stm32_clock_pll(&oscillator, target, &source);
        if (result != HAL_OK) {
            return result;
        }
        if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) {
            return HAL_ERROR;
        }
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

    /* 高频运行时先切到 VOS0；低功耗稳压器由板级自行调整。 */
#if defined(PWR_REGULATOR_VOLTAGE_SCALE0)
    if (target > 200000000U) {
        (void)HAL_PWREx_ConfigSupply(ROS_H7_PWR_SUPPLY);
        if (HAL_PWREx_ControlVoltageScaling(
                PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK) {
            return HAL_ERROR;
        }
    }
#endif

    /* 等待周期按每 100MHz 约 1WS 估算（VOS0 下足够保守）。 */
    wait_states = (target + 99999999U) / 100000000U;
    if (wait_states > 15U) {
        wait_states = 15U;
    }

    clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                      RCC_CLOCKTYPE_PCLK3 | RCC_CLOCKTYPE_PCLK4;
    clock.SYSCLKSource = source;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = stm32_apb_divider(target, 120000000U);
    clock.APB2CLKDivider = stm32_apb_divider(target, 120000000U);
    clock.APB3CLKDivider = RCC_APB3_DIV1;
    clock.APB4CLKDivider = RCC_APB4_DIV1;

    return ros_cube_status(HAL_RCC_ClockConfig(&clock, wait_states));
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
        return HAL_RCC_GetPCLK3Freq();
    case HAL_CLOCK_APB4:
        return HAL_RCC_GetPCLK4Freq();
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

#endif /* ROS_STM32_H7 */
