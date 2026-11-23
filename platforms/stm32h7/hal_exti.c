/*
 * hal_exti.c - STM32H7 EXTI HAL 适配
 *
 * 由原单文件 stm32_f4xx.c 按模块拆分。
 */

#if defined(ROS_STM32_H7)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

typedef struct {
    bool used;
    hal_pin_t pin;
    hal_exti_callback_t callback;
    void *user_data;
} stm32_exti_slot_t;

static stm32_exti_slot_t s_stm32_exti[16];

static IRQn_Type stm32_exti_irqn(uint32_t index)
{
    if (index <= 4U) {
        return (IRQn_Type)(EXTI0_IRQn + (int32_t)index);
    }
    if (index <= 9U) {
        return EXTI9_5_IRQn;
    }
    return EXTI15_10_IRQn;
}

int hal_exti_init(hal_pin_t pin, const hal_exti_config_t *config)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef *port;
    IRQn_Type irqn;
    uint32_t index;

    if (config == NULL) {
        return HAL_INVALID_PARAM;
    }

    port = stm32_gpio_port(HAL_PIN_GET_PORT(pin));
    index = HAL_PIN_GET_INDEX(pin);
    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }

    memset(&init, 0, sizeof(init));
    init.Pin = stm32_pin_mask(index);
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;

    switch (config->trigger) {
    case HAL_EXTI_RISING:
        init.Mode = GPIO_MODE_IT_RISING;
        break;
    case HAL_EXTI_FALLING:
        init.Mode = GPIO_MODE_IT_FALLING;
        break;
    case HAL_EXTI_BOTH:
        init.Mode = GPIO_MODE_IT_RISING_FALLING;
        break;
    default:
        return HAL_INVALID_PARAM;
    }

    HAL_GPIO_Init(port, &init);

    irqn = stm32_exti_irqn(index);
    HAL_NVIC_SetPriority(irqn, config->priority, 0U);
    HAL_NVIC_EnableIRQ(irqn);
    __HAL_GPIO_EXTI_CLEAR_IT(stm32_pin_mask(index));

    s_stm32_exti[index].used = true;
    s_stm32_exti[index].pin = pin;
    s_stm32_exti[index].callback = config->callback;
    s_stm32_exti[index].user_data = config->user_data;

    return HAL_OK;
}

int hal_exti_enable(hal_pin_t pin)
{
    uint32_t index = HAL_PIN_GET_INDEX(pin);
    uint16_t mask;

    if (index > 15U) {
        return HAL_INVALID_PARAM;
    }
    mask = stm32_pin_mask(index);
    EXTI->IMR1 |= (uint32_t)mask;
    HAL_NVIC_EnableIRQ(stm32_exti_irqn(index));
    return HAL_OK;
}

int hal_exti_disable(hal_pin_t pin)
{
    uint32_t index = HAL_PIN_GET_INDEX(pin);
    uint16_t mask;

    if (index > 15U) {
        return HAL_INVALID_PARAM;
    }
    mask = stm32_pin_mask(index);
    EXTI->IMR1 &= ~(uint32_t)mask;
    HAL_NVIC_DisableIRQ(stm32_exti_irqn(index));
    return HAL_OK;
}

int hal_exti_clear(hal_pin_t pin)
{
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if (index > 15U) {
        return HAL_INVALID_PARAM;
    }
    __HAL_GPIO_EXTI_CLEAR_IT(stm32_pin_mask(index));
    HAL_NVIC_ClearPendingIRQ(stm32_exti_irqn(index));
    return HAL_OK;
}

int hal_exti_deinit(hal_pin_t pin)
{
    GPIO_TypeDef *port = stm32_gpio_port(HAL_PIN_GET_PORT(pin));
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }

    (void)hal_exti_disable(pin);
    HAL_GPIO_DeInit(port, stm32_pin_mask(index));
    s_stm32_exti[index].used = false;
    s_stm32_exti[index].callback = NULL;
    s_stm32_exti[index].user_data = NULL;
    return HAL_OK;
}

/*
 * 直接提供强定义 HAL_GPIO_EXTI_Callback。
 * 若用户工程中也定义了同名函数，会产生重复符号；此时请删除用户定义，
 * 或改为在用户中断服务函数中直接调用上层回调。
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint32_t i;

    for (i = 0U; i < 16U; i++) {
        if ((GPIO_Pin & stm32_pin_mask(i)) != 0U) {
            if (s_stm32_exti[i].used && (s_stm32_exti[i].callback != NULL)) {
                s_stm32_exti[i].callback(s_stm32_exti[i].pin,
                                      s_stm32_exti[i].user_data);
            }
        }
    }
}


#endif /* ROS_STM32_H7 */
