/*
 * hal_exti.c - STM32F1 EXTI 寄存器实现（不依赖 Cube HAL）
 *
 * 通过 AFIO->EXTICR 选择端口，EXTI 配置边沿触发并注册回调。
 * 共用中断入口：EXTI0~4 独立入口，EXTI5~9 与 EXTI10~15 共用入口。
 */

#if defined(ROS_STM32_F1)

#include "hal.h"
#include "stm32f10x.h"

#define STM32_RCC_APB2ENR_AFIOEN   (1UL << 0)
#define STM32_RCC_APB2ENR_IOPAEN   (1UL << 2)

typedef struct {
    bool used;
    hal_pin_t pin;
    hal_exti_callback_t callback;
    void *user_data;
} stm32_exti_slot_t;

static stm32_exti_slot_t s_stm32_exti[16];

static void stm32_exti_port_select(uint32_t index, uint32_t port)
{
    uint32_t reg = index / 4U;
    uint32_t shift = (index % 4U) * 4U;

    AFIO->EXTICR[reg] = (AFIO->EXTICR[reg] & ~(0x0FUL << shift)) |
                        ((port & 0x0FUL) << shift);
}

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
    hal_gpio_config_t gpio;
    uint32_t index = HAL_PIN_GET_INDEX(pin);
    uint32_t port = HAL_PIN_GET_PORT(pin);
    uint32_t mask;

    if (config == NULL || index > 15U || port > 6U) {
        return HAL_INVALID_PARAM;
    }

    /* AFIO 与对应 GPIO 端口时钟 */
    RCC->APB2ENR |= STM32_RCC_APB2ENR_AFIOEN | (STM32_RCC_APB2ENR_IOPAEN << port);

    /* 输入模式，触发边沿由 EXTI 配置 */
    gpio.mode = HAL_GPIO_MODE_INPUT;
    gpio.output = HAL_GPIO_OUTPUT_PP;
    gpio.pull = HAL_GPIO_NOPULL;
    gpio.speed = HAL_GPIO_SPEED_LOW;
    gpio.alternate = 0U;
    gpio.initial_level = HAL_GPIO_LOW;
    if (hal_gpio_init(pin, &gpio) != HAL_OK) {
        return HAL_INVALID_PARAM;
    }

    mask = 1UL << index;
    EXTI->IMR &= ~mask;
    EXTI->EMR &= ~mask;
    EXTI->RTSR &= ~mask;
    EXTI->FTSR &= ~mask;

    switch (config->trigger) {
    case HAL_EXTI_RISING:
        EXTI->RTSR |= mask;
        break;
    case HAL_EXTI_FALLING:
        EXTI->FTSR |= mask;
        break;
    case HAL_EXTI_BOTH:
        EXTI->RTSR |= mask;
        EXTI->FTSR |= mask;
        break;
    default:
        return HAL_INVALID_PARAM;
    }

    stm32_exti_port_select(index, port);
    EXTI->PR = mask;

    s_stm32_exti[index].used = true;
    s_stm32_exti[index].pin = pin;
    s_stm32_exti[index].callback = config->callback;
    s_stm32_exti[index].user_data = config->user_data;

    NVIC_SetPriority(stm32_exti_irqn(index), config->priority);
    NVIC_EnableIRQ(stm32_exti_irqn(index));
    return HAL_OK;
}

int hal_exti_enable(hal_pin_t pin)
{
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if (index > 15U || !s_stm32_exti[index].used) {
        return HAL_NOT_READY;
    }
    EXTI->IMR |= (1UL << index);
    NVIC_EnableIRQ(stm32_exti_irqn(index));
    return HAL_OK;
}

int hal_exti_disable(hal_pin_t pin)
{
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if (index > 15U) {
        return HAL_INVALID_PARAM;
    }
    EXTI->IMR &= ~(1UL << index);
    NVIC_DisableIRQ(stm32_exti_irqn(index));
    return HAL_OK;
}

int hal_exti_clear(hal_pin_t pin)
{
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if (index > 15U) {
        return HAL_INVALID_PARAM;
    }
    EXTI->PR = (1UL << index);
    NVIC_ClearPendingIRQ(stm32_exti_irqn(index));
    return HAL_OK;
}

int hal_exti_deinit(hal_pin_t pin)
{
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if (index > 15U) {
        return HAL_INVALID_PARAM;
    }
    (void)hal_exti_disable(pin);
    EXTI->PR = (1UL << index);
    stm32_exti_port_select(index, 0U);
    s_stm32_exti[index].used = false;
    s_stm32_exti[index].callback = NULL;
    s_stm32_exti[index].user_data = NULL;
    return HAL_OK;
}

static void stm32_exti_dispatch(uint32_t first, uint32_t last)
{
    uint32_t pending = EXTI->PR;
    uint32_t i;

    for (i = first; i <= last; i++) {
        uint32_t mask = 1UL << i;

        if ((pending & mask) == 0U) {
            continue;
        }
        EXTI->PR = mask;
        if (s_stm32_exti[i].used && (s_stm32_exti[i].callback != NULL)) {
            s_stm32_exti[i].callback(s_stm32_exti[i].pin,
                                     s_stm32_exti[i].user_data);
        }
    }
}

void EXTI0_IRQHandler(void)
{
    stm32_exti_dispatch(0U, 0U);
}

void EXTI1_IRQHandler(void)
{
    stm32_exti_dispatch(1U, 1U);
}

void EXTI2_IRQHandler(void)
{
    stm32_exti_dispatch(2U, 2U);
}

void EXTI3_IRQHandler(void)
{
    stm32_exti_dispatch(3U, 3U);
}

void EXTI4_IRQHandler(void)
{
    stm32_exti_dispatch(4U, 4U);
}

void EXTI9_5_IRQHandler(void)
{
    stm32_exti_dispatch(5U, 9U);
}

void EXTI15_10_IRQHandler(void)
{
    stm32_exti_dispatch(10U, 15U);
}

#endif /* ROS_STM32_F1 */
