/*
 * STM32 平台 —— 系统级接口（hal_os.h 实现）。
 *
 * 时基与毫秒延时由 HAL 提供（SysTick），微秒延时使用 DWT 周期计数器；
 * DWT 在首次调用 hal_delay_us() 时惰性初始化。
 *
 * 系列选择宏（编译目标中定义其一）：
 *   IOT_STM32_F1 / IOT_STM32_F4 / IOT_STM32_H7
 */

#include "hal.h"

#if defined(IOT_STM32_H7)
#include "stm32h7xx_hal.h"
#elif defined(IOT_STM32_F4)
#include "stm32f4xx_hal.h"
#elif defined(IOT_STM32_F1)
#include "stm32f1xx_hal.h"
#else
#error "请定义 IOT_STM32_F1 / IOT_STM32_F4 / IOT_STM32_H7 之一"
#endif

static bool s_dwt_ready = false;

static void stm32_dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_dwt_ready = true;
}

uint32_t hal_tick_get(void)
{
    return (uint32_t)HAL_GetTick();
}

void hal_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

void hal_delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t cycles;

    if (us == 0U) {
        return;
    }
    if (!s_dwt_ready) {
        stm32_dwt_init();
    }
    start = DWT->CYCCNT;
    cycles = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < cycles) {
        ;
    }
}

void hal_irq_enable(void)
{
    __enable_irq();
}

void hal_irq_disable(void)
{
    __disable_irq();
}

void hal_system_reset(void)
{
    NVIC_SystemReset();
}

void hal_system_sleep(void)
{
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
}

void hal_system_deepsleep(void)
{
#if defined(IOT_STM32_H7)
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
#else
    HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI);
#endif
}
