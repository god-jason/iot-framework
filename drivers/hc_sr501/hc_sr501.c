#include "hc_sr501.h"
#include "log.h"

#define HC_SR501_TAG "hc_sr501"

static int hc_sr501_check(const hc_sr501_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int hc_sr501_init(hc_sr501_handle_t *handle, hal_pin_t pin)
{
    if (handle == NULL || pin == 0U) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->pin = pin;
    handle->initialized = false;

    if (driver_gpio_input(pin, HAL_GPIO_PULLDOWN) != DRIVER_OK) {
        LOG_E(HC_SR501_TAG, "gpio init failed");
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I(HC_SR501_TAG, "init ok, warmup %u ms", (unsigned int)HC_SR501_WARMUP_MS);
    return DRIVER_OK;
}

int hc_sr501_deinit(hc_sr501_handle_t *handle)
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

int hc_sr501_read(hc_sr501_handle_t *handle, bool *motion)
{
    int level;
    int ret;

    if (motion == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = hc_sr501_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    level = driver_gpio_read(handle->pin);
    if (level < 0) {
        LOG_E(HC_SR501_TAG, "gpio read failed");
        return (int)HAL_IO_ERROR;
    }

    *motion = (level == (int)HC_SR501_ACTIVE_LEVEL);
    return DRIVER_OK;
}
