#include "vibration.h"
#include "log.h"

#define VIBRATION_TAG "vibration"

static int vibration_check(const vibration_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int vibration_init(vibration_handle_t *handle, iot_pin_t pin)
{
    if (handle == NULL || pin == 0U) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->pin = pin;
    handle->initialized = false;

    if (driver_gpio_input(pin, IOT_GPIO_PULLDOWN) != DRIVER_OK) {
        LOG_E(VIBRATION_TAG, "gpio init failed");
        return (int)IOT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I(VIBRATION_TAG, "init ok");
    return DRIVER_OK;
}

int vibration_deinit(vibration_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->pin != 0U) {
        (void)driver_gpio_deinit(handle->pin);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int vibration_read(vibration_handle_t *handle, bool *vibrating)
{
    uint8_t i;
    int ret;

    if (vibrating == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = vibration_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    *vibrating = false;
    for (i = 0U; i < VIBRATION_SAMPLES; i++) {
        int level = driver_gpio_read(handle->pin);

        if (level < 0) {
            LOG_E(VIBRATION_TAG, "gpio read failed");
            return (int)IOT_IO_ERROR;
        }
        if (level == (int)VIBRATION_ACTIVE_LEVEL) {
            *vibrating = true;
            break;
        }
        driver_delay_us(VIBRATION_SAMPLE_US);
    }

    return DRIVER_OK;
}
