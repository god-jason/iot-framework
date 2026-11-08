#include "am312.h"
#include "log.h"

#define AM312_TAG "am312"

static int am312_check(const am312_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int am312_init(am312_handle_t *handle, hal_pin_t pin)
{
    if (handle == NULL || pin == 0U) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->pin = pin;
    handle->initialized = false;

    /* 下拉：模块未接或掉电时读到低电平（无移动），避免悬空误报 */
    if (driver_gpio_input(pin, HAL_GPIO_PULLDOWN) != DRIVER_OK) {
        LOG_E(AM312_TAG, "gpio init failed");
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I(AM312_TAG, "init ok, warmup %u ms", (unsigned int)AM312_WARMUP_MS);
    return DRIVER_OK;
}

int am312_deinit(am312_handle_t *handle)
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

int am312_read(am312_handle_t *handle, bool *motion)
{
    int level;
    int ret;

    if (motion == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = am312_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    level = driver_gpio_read(handle->pin);
    if (level < 0) {
        LOG_E(AM312_TAG, "gpio read failed");
        return (int)HAL_IO_ERROR;
    }

    *motion = (level == (int)AM312_ACTIVE_LEVEL);
    return DRIVER_OK;
}
