/*
 * hal_wdg.c - STM32F1 IWDG / WWDG 寄存器实现（不依赖 Cube HAL）
 */

#if defined(ROS_STM32_F1)

#include "hal.h"
#include "stm32f10x.h"

#define STM32_IWDG_KEY_ENABLE   0xCCCCU
#define STM32_IWDG_KEY_RELOAD   0xAAAAU
#define STM32_IWDG_KEY_ACCESS   0x5555U

#define STM32_RCC_CSR_IWDGRSTF  (1UL << 29)
#define STM32_RCC_CSR_WWDGRSTF  (1UL << 30)
#define STM32_RCC_CSR_RMVF      (1UL << 24)

#define STM32_IWDG_LSI_HZ       40000U

static uint32_t stm32_pclk1(void)
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

/* IWDG：返回 PR 编码，reload 由输出参数带回。 */
static uint32_t stm32_iwdg_calc(uint32_t timeout_ms, uint32_t *reload)
{
    uint32_t total = timeout_ms * (STM32_IWDG_LSI_HZ / 1000U);
    uint32_t pr;

    if (total == 0U) {
        total = 1U;
    }
    for (pr = 0U; pr < 7U; pr++) {
        uint32_t divider = 4U << pr;
        uint32_t rlr = total / divider;

        if (rlr <= 0xFFFU) {
            *reload = (rlr == 0U) ? 1U : rlr;
            return pr;
        }
    }
    *reload = 0xFFFU;
    return 6U;
}

static uint32_t stm32_wwdg_calc(uint32_t timeout_ms, uint32_t window_ms,
                                uint32_t *window)
{
    uint32_t pclk = stm32_pclk1();
    uint32_t wdgtb;
    uint32_t counter = 0U;

    for (wdgtb = 0U; wdgtb < 4U; wdgtb++) {
        uint32_t ticks = (pclk / 4096U) >> wdgtb;

        if (ticks != 0U) {
            uint32_t t = (ticks / 1000U) * timeout_ms;

            if (t <= 64U) {
                counter = (t == 0U) ? 1U : t;
                break;
            }
        }
        counter = 64U;
    }

    if (counter == 0U) {
        counter = 64U;
    }
    if (wdgtb > 3U) {
        wdgtb = 3U;
    }
    *window = counter;
    if (window_ms > 0U) {
        uint32_t ticks = (pclk / 4096U) >> wdgtb;

        if (ticks != 0U) {
            uint32_t w = (ticks / 1000U) * window_ms;

            if (w < counter) {
                *window = (w == 0U) ? 1U : w;
            }
        }
    }
    return wdgtb;
}

int hal_wdg_init(hal_wdg_t *handle, const hal_wdg_config_t *config)
{
    uint32_t reload;
    uint32_t pr;
    uint32_t window;
    uint32_t wdgtb;

    if ((handle == NULL) || (config == NULL) || (config->timeout_ms == 0U)) {
        return HAL_INVALID_PARAM;
    }

    if (config->type == HAL_WDG_INDEPENDENT) {
        pr = stm32_iwdg_calc(config->timeout_ms, &reload);

        IWDG->KR = STM32_IWDG_KEY_ACCESS;
        IWDG->PR = pr;
        IWDG->RLR = reload;
        while ((IWDG->SR & 0x3U) != 0U) {
            ;
        }
        IWDG->KR = STM32_IWDG_KEY_RELOAD;
    } else if (config->type == HAL_WDG_WINDOW) {
        wdgtb = stm32_wwdg_calc(config->timeout_ms, config->window_ms,
                               &window);
        WWDG->CFR = (window & 0x7FU) | ((wdgtb & 0x3U) << 7);
        WWDG->CR = (window & 0x7FU);
    } else {
        return HAL_INVALID_PARAM;
    }

    handle->config = *config;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_wdg_start(hal_wdg_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }

    if (handle->config.type == HAL_WDG_INDEPENDENT) {
        IWDG->KR = STM32_IWDG_KEY_ENABLE;
    } else {
        WWDG->CR |= (1UL << 7);
    }
    return HAL_OK;
}

int hal_wdg_refresh(hal_wdg_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }

    if (handle->config.type == HAL_WDG_INDEPENDENT) {
        IWDG->KR = STM32_IWDG_KEY_RELOAD;
    } else {
        /* 窗口内喂狗：写回窗口值（低位 7 位），保留 WDGA */
        uint32_t counter = WWDG->CR & 0x7FU;
        uint32_t window = WWDG->CFR & 0x7FU;

        if (counter > window) {
            return HAL_PERMISSION;
        }
        /* 刷新到最大计数值 0x7F，保留 WDGA。 */
        WWDG->CR = (WWDG->CR & 0x80U) | 0x7FU;
    }
    return HAL_OK;
}

int hal_wdg_stop(hal_wdg_t *handle)
{
    HAL_UNUSED(handle);
    /* F1 的 IWDG 一旦启动无法停止；WWDG 同样只能复位清除。 */
    return HAL_NOT_SUPPORTED;
}

bool hal_wdg_is_reset(void)
{
    return ((RCC->CSR & (STM32_RCC_CSR_IWDGRSTF |
                         STM32_RCC_CSR_WWDGRSTF)) != 0U);
}

void hal_wdg_clear_reset_flag(void)
{
    RCC->CSR |= STM32_RCC_CSR_RMVF;
}

#endif /* ROS_STM32_F1 */
