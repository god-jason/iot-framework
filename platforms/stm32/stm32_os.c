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
