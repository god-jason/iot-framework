#ifndef ROS_STM32_INTERNAL_H
#define ROS_STM32_INTERNAL_H

/*
 * stm32_internal.h - STM32 平台模块间共享的内部辅助
 *
 * 仅平台层实现文件使用，不属于公共 HAL API。
 * 当前提供 GPIO 端口映射与引脚掩码（GPIO / EXTI 模块共用）。
 */

#include "hal.h"
#include "stm32_cube.h"

#if defined(ROS_STM32_F4)
#ifndef ROS_STM32_FLASH_SIZE
#define ROS_STM32_FLASH_SIZE (1024U * 1024U)
#endif

#ifndef ROS_STM32_FLASH_SECTOR_SIZE
#define ROS_STM32_FLASH_SECTOR_SIZE (16U * 1024U)
#endif
#elif defined(ROS_STM32_H7)
#ifndef ROS_STM32_FLASH_SIZE
#define ROS_STM32_FLASH_SIZE (2048U * 1024U)
#endif

#ifndef ROS_STM32_FLASH_SECTOR_SIZE
#define ROS_STM32_FLASH_SECTOR_SIZE (128U * 1024U)
#endif
#endif

#if defined(ROS_STM32_F4) || defined(ROS_STM32_H7)
static inline GPIO_TypeDef *stm32_gpio_port(uint32_t index)
{
    switch (index) {
    case 0U:
        return GPIOA;
    case 1U:
        return GPIOB;
#ifdef GPIOC
    case 2U:
        return GPIOC;
#endif
#ifdef GPIOD
    case 3U:
        return GPIOD;
#endif
#ifdef GPIOE
    case 4U:
        return GPIOE;
#endif
#ifdef GPIOF
    case 5U:
        return GPIOF;
#endif
#ifdef GPIOG
    case 6U:
        return GPIOG;
#endif
#ifdef GPIOH
    case 7U:
        return GPIOH;
#endif
#ifdef GPIOI
    case 8U:
        return GPIOI;
#endif
#ifdef GPIOJ
    case 9U:
        return GPIOJ;
#endif
#ifdef GPIOK
    case 10U:
        return GPIOK;
#endif
    default:
        return NULL;
    }
}

static inline uint16_t stm32_pin_mask(uint32_t index)
{
    return (uint16_t)(1UL << index);
}
#endif /* ROS_STM32_F4 || ROS_STM32_H7 */

#endif /* ROS_STM32_INTERNAL_H */
