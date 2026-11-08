#include "a3144.h"
#include "log.h"

#define A3144_TAG "a3144"

static int a3144_check(const a3144_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int a3144_init(a3144_handle_t *handle, hal_pin_t pin)
{
    if (handle == NULL || pin == 0U) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->pin = pin;
    handle->initialized = false;

    /* 开漏输出需要上拉，否则悬空时读到随机电平 */
    if (driver_gpio_input(pin, HAL_GPIO_PULLUP) != DRIVER_OK) {
        LOG_E(A3144_TAG, "gpio init failed");
        return (int)HAL_IO_ERROR;
    }
    driver_delay_us(A3144_SETTLE_US);

    handle->initialized = true;
    LOG_I(A3144_TAG, "init ok");
    return DRIVER_OK;
}

int a3144_deinit(a3144_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->pin != 0U) {
        (void)driver_gpio_deinit(handle->pin);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int a3144_read(a3144_handle_t *handle, bool *detected)
{
    int level;
    int ret;

    if (detected == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = a3144_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    level = driver_gpio_read(handle->pin);
    if (level < 0) {
        LOG_E(A3144_TAG, "gpio read failed");
        return (int)HAL_IO_ERROR;
    }

    *detected = (level == (int)A3144_ACTIVE_LEVEL);
    return DRIVER_OK;
}
