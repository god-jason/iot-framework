/*
 * STM32 平台 —— 系统级接口（hal_mcu.h 实现）。
 *
 * 时基由 HAL SysTick 提供；毫秒延时统一使用内核 ros_sleep_ms()。
 *
 * 系列选择宏（编译目标中定义其一）：
 *   ROS_STM32_F1 / ROS_STM32_F4 / ROS_STM32_H7
 */

#include "hal.h"

#if !defined(ROS_STM32_F1) && !defined(ROS_STM32_F4) && !defined(ROS_STM32_H7)
#error "请定义 ROS_STM32_F1 / ROS_STM32_F4 / ROS_STM32_H7 之一"
#endif

/* Cube 头文件与框架的 HAL_OK 等常量同名，统一由该包装头处理 */
#include "stm32_cube.h"

uint32_t hal_tick_get(void)
{
    return (uint32_t)HAL_GetTick();
}

/*
 * 微秒忙等：用 DWT 周期计数器（Cortex-M3/M4/M7 均提供），
 * 首次调用时使能，之后只需读 CYCCNT，可在关中断的临界区内使用。
 */
void hal_delay_us(uint32_t us)
{
    uint32_t cycles;
    uint32_t start;

    if (us == 0U) {
        return;
    }

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }

    while (us > 1000U) {                   /* 分段，避免 cycles 溢出 */
        hal_delay_us(1000U);
        us -= 1000U;
    }

    cycles = us * (SystemCoreClock / 1000000U);
    if (cycles == 0U) {
        cycles = 1U;
    }
    start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
    }
}

uint32_t hal_irq_save(void)
{
    uint32_t primask;

    __asm volatile ("mrs %0, primask" : "=r" (primask) :: "memory");
    __disable_irq();
    return primask;
}

void hal_irq_restore(uint32_t state)
{
    __set_PRIMASK(state & 1U);
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
#if defined(ROS_STM32_H7)
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
#else
    HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI);
#endif
}
