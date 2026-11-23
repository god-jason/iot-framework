/*
 * hal_gpio.c - STM32F1 GPIO / PIN 寄存器实现（不依赖 Cube HAL）
 *
 * 覆盖 PA/PB/PC/PD，输入（浮空/上拉/下拉）、推挽/开漏输出、模拟、复用。
 * 句柄约定与 hal_gpio.h 一致。
 */

#if defined(ROS_STM32_F1)

#include "hal.h"
#include "stm32f10x.h"

#define STM32_RCC_APB2ENR_IOPAEN   (1UL << 2)
#define STM32_RCC_APB2ENR_IOPBEN   (1UL << 3)
#define STM32_RCC_APB2ENR_IOPCEN   (1UL << 4)
#define STM32_RCC_APB2ENR_IOPDEN   (1UL << 5)

static GPIO_TypeDef *stm32_gpio_port(hal_pin_t pin)
{
    switch (HAL_PIN_GET_PORT(pin)) {
    case 0U:
        RCC->APB2ENR |= STM32_RCC_APB2ENR_IOPAEN;
        return GPIOA;
    case 1U:
        RCC->APB2ENR |= STM32_RCC_APB2ENR_IOPBEN;
        return GPIOB;
    case 2U:
        RCC->APB2ENR |= STM32_RCC_APB2ENR_IOPCEN;
        return GPIOC;
    case 3U:
        RCC->APB2ENR |= STM32_RCC_APB2ENR_IOPDEN;
        return GPIOD;
    default:
        return NULL;
    }
}

/* 写 CRL/CRH 中某个引脚的 4 位配置（MODE[1:0] + CNF[1:0]） */
static void stm32_gpio_nibble(GPIO_TypeDef *port, uint32_t index,
                              uint32_t nibble)
{
    if (index < 8U) {
        uint32_t shift = index * 4U;

        port->CRL = (port->CRL & ~(0x0FUL << shift)) | (nibble << shift);
    } else {
        uint32_t shift = (index - 8U) * 4U;

        port->CRH = (port->CRH & ~(0x0FUL << shift)) | (nibble << shift);
    }
}

/* 输出速度 -> F1 MODE：10MHz=01，2MHz=10，50MHz=11 */
static uint32_t stm32_gpio_speed(hal_gpio_speed_e speed)
{
    switch (speed) {
    case HAL_GPIO_SPEED_LOW:      return 0x2U;
    case HAL_GPIO_SPEED_MEDIUM:   return 0x1U;
    case HAL_GPIO_SPEED_HIGH:
    case HAL_GPIO_SPEED_VERY_HIGH:
    default:                      return 0x3U;
    }
}

int hal_gpio_init(hal_pin_t pin, const hal_gpio_config_t *config)
{
    GPIO_TypeDef *port = stm32_gpio_port(pin);
    uint32_t index = HAL_PIN_GET_INDEX(pin);
    uint32_t nibble;

    if (port == NULL || config == NULL || index > 15U) {
        return HAL_INVALID_PARAM;
    }

    switch (config->mode) {
    case HAL_GPIO_MODE_INPUT:
        if (config->pull == HAL_GPIO_PULLUP) {
            port->ODR |= (1UL << index);            /* ODR=1 -> 上拉 */
            nibble = 0x8U;
        } else if (config->pull == HAL_GPIO_PULLDOWN) {
            port->ODR &= ~(1UL << index);           /* ODR=0 -> 下拉 */
            nibble = 0x8U;
        } else {
            nibble = 0x4U;                          /* 浮空输入 */
        }
        break;

    case HAL_GPIO_MODE_OUTPUT:
        nibble = stm32_gpio_speed(config->speed) |
                 ((config->output == HAL_GPIO_OUTPUT_OD) ? 0x4U : 0x0U);
        if (config->initial_level == HAL_GPIO_HIGH) {
            port->BSRR = (1UL << index);
        } else {
            port->BSRR = (1UL << (index + 16U));
        }
        break;

    case HAL_GPIO_MODE_AF:
        nibble = stm32_gpio_speed(config->speed) | 0x8U;  /* 复用推挽 */
        break;

    case HAL_GPIO_MODE_ANALOG:
    default:
        nibble = 0x0U;                              /* 模拟输入 */
        break;
    }

    stm32_gpio_nibble(port, index, nibble);
    return HAL_OK;
}

int hal_gpio_deinit(hal_pin_t pin)
{
    GPIO_TypeDef *port = stm32_gpio_port(pin);
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if (port == NULL || index > 15U) {
        return HAL_INVALID_PARAM;
    }
    stm32_gpio_nibble(port, index, 0x4U);          /* 浮空输入 */
    return HAL_OK;
}

int hal_gpio_write(hal_pin_t pin, hal_gpio_level_e level)
{
    GPIO_TypeDef *port = stm32_gpio_port(pin);
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if (port == NULL || index > 15U) {
        return HAL_INVALID_PARAM;
    }
    port->BSRR = (level == HAL_GPIO_HIGH) ? (1UL << index)
                                          : (1UL << (index + 16U));
    return HAL_OK;
}

int hal_gpio_read(hal_pin_t pin, hal_gpio_level_e *level)
{
    GPIO_TypeDef *port = stm32_gpio_port(pin);
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if (port == NULL || level == NULL || index > 15U) {
        return HAL_INVALID_PARAM;
    }
    *level = ((port->IDR & (1UL << index)) != 0U) ? HAL_GPIO_HIGH
                                                  : HAL_GPIO_LOW;
    return HAL_OK;
}

int hal_gpio_toggle(hal_pin_t pin)
{
    GPIO_TypeDef *port = stm32_gpio_port(pin);
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if (port == NULL || index > 15U) {
        return HAL_INVALID_PARAM;
    }
    port->ODR ^= (1UL << index);
    return HAL_OK;
}

int hal_gpio_set(hal_pin_t pin)
{
    return hal_gpio_write(pin, HAL_GPIO_HIGH);
}

int hal_gpio_reset(hal_pin_t pin)
{
    return hal_gpio_write(pin, HAL_GPIO_LOW);
}

#endif /* ROS_STM32_F1 */
