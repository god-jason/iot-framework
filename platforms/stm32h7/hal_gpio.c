/*
 * hal_gpio.c - STM32H7 GPIO / PIN HAL 适配（HAL2 骨架）
 *
 * 与 F4 的 GPIO 模型一致：用户提供 HAL 句柄，中断由用户 IRQHandler 转发。
 * H7 的 GPIO 寄存器与 API 与 F4 基本一致，因此这里的实现可直接用于 HAL2。
 */

#if defined(ROS_STM32_H7)

#include "hal.h"
#include "stm32.h"
#include "stm32_cube.h"
#include "stm32_internal.h"

#include <string.h>

static uint32_t stm32_gpio_speed(hal_gpio_speed_e speed)
{
    switch (speed) {
    case HAL_GPIO_SPEED_LOW:
        return GPIO_SPEED_FREQ_LOW;
    case HAL_GPIO_SPEED_MEDIUM:
        return GPIO_SPEED_FREQ_MEDIUM;
    case HAL_GPIO_SPEED_HIGH:
        return GPIO_SPEED_FREQ_HIGH;
    case HAL_GPIO_SPEED_VERY_HIGH:
        return GPIO_SPEED_FREQ_VERY_HIGH;
    default:
        return GPIO_SPEED_FREQ_LOW;
    }
}

static uint32_t stm32_gpio_pull(hal_gpio_pull_e pull)
{
    switch (pull) {
    case HAL_GPIO_PULLUP:
        return GPIO_PULLUP;
    case HAL_GPIO_PULLDOWN:
        return GPIO_PULLDOWN;
    case HAL_GPIO_NOPULL:
    default:
        return GPIO_NOPULL;
    }
}

int hal_gpio_init(hal_pin_t pin, const hal_gpio_config_t *config)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef *port;
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
    init.Pull = stm32_gpio_pull(config->pull);
    init.Speed = stm32_gpio_speed(config->speed);

    switch (config->mode) {
    case HAL_GPIO_MODE_INPUT:
        init.Mode = GPIO_MODE_INPUT;
        break;
    case HAL_GPIO_MODE_OUTPUT:
        init.Mode = (config->output == HAL_GPIO_OUTPUT_OD) ?
                    GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT_PP;
        HAL_GPIO_WritePin(port, init.Pin,
                          (config->initial_level == HAL_GPIO_HIGH) ?
                          GPIO_PIN_SET : GPIO_PIN_RESET);
        break;
    case HAL_GPIO_MODE_ANALOG:
        init.Mode = GPIO_MODE_ANALOG;
        init.Pull = GPIO_NOPULL;
        break;
    case HAL_GPIO_MODE_AF:
        init.Mode = (config->output == HAL_GPIO_OUTPUT_OD) ?
                    GPIO_MODE_AF_OD : GPIO_MODE_AF_PP;
        init.Alternate = config->alternate;
        break;
    default:
        return HAL_INVALID_PARAM;
    }

    HAL_GPIO_Init(port, &init);
    return HAL_OK;
}

int hal_gpio_deinit(hal_pin_t pin)
{
    GPIO_TypeDef *port = stm32_gpio_port(HAL_PIN_GET_PORT(pin));
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }
    HAL_GPIO_DeInit(port, stm32_pin_mask(index));
    return HAL_OK;
}

int hal_gpio_write(hal_pin_t pin, hal_gpio_level_e level)
{
    GPIO_TypeDef *port = stm32_gpio_port(HAL_PIN_GET_PORT(pin));
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }
    HAL_GPIO_WritePin(port, stm32_pin_mask(index),
                      (level == HAL_GPIO_HIGH) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return HAL_OK;
}

int hal_gpio_read(hal_pin_t pin, hal_gpio_level_e *level)
{
    GPIO_TypeDef *port = stm32_gpio_port(HAL_PIN_GET_PORT(pin));
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if ((port == NULL) || (index > 15U) || (level == NULL)) {
        return HAL_INVALID_PARAM;
    }
    *level = (HAL_GPIO_ReadPin(port, stm32_pin_mask(index)) == GPIO_PIN_SET) ?
             HAL_GPIO_HIGH : HAL_GPIO_LOW;
    return HAL_OK;
}

int hal_gpio_toggle(hal_pin_t pin)
{
    GPIO_TypeDef *port = stm32_gpio_port(HAL_PIN_GET_PORT(pin));
    uint32_t index = HAL_PIN_GET_INDEX(pin);

    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }
    HAL_GPIO_TogglePin(port, stm32_pin_mask(index));
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

int hal_pin_init(const hal_pin_config_t *config)
{
    GPIO_InitTypeDef init;
    GPIO_TypeDef *port;
    uint32_t index;

    if (config == NULL) {
        return HAL_INVALID_PARAM;
    }

    port = stm32_gpio_port(HAL_PIN_GET_PORT(config->pin));
    index = HAL_PIN_GET_INDEX(config->pin);
    if ((port == NULL) || (index > 15U)) {
        return HAL_INVALID_PARAM;
    }

    memset(&init, 0, sizeof(init));
    init.Pin = stm32_pin_mask(index);
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;

    switch (config->function) {
    case HAL_PIN_FUNCTION_GPIO:
        init.Mode = GPIO_MODE_INPUT;
        break;
    case HAL_PIN_FUNCTION_AF:
        init.Mode = GPIO_MODE_AF_PP;
        init.Alternate = config->alternate;
        break;
    case HAL_PIN_FUNCTION_ANALOG:
        init.Mode = GPIO_MODE_ANALOG;
        break;
    default:
        return HAL_INVALID_PARAM;
    }

    HAL_GPIO_Init(port, &init);
    return HAL_OK;
}

int hal_pin_deinit(hal_pin_t pin)
{
    return hal_gpio_deinit(pin);
}

#endif /* ROS_STM32_H7 */
