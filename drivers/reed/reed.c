#include "reed.h"
#include "log.h"

#define REED_TAG "reed"

static int reed_check(const reed_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int reed_init(reed_handle_t *handle, port_pin_t pin)
{
    if (handle == NULL || pin == 0U) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->pin = pin;
    handle->initialized = false;

    /* 上拉：触点断开时为高，闭合时被拉低 */
    if (driver_gpio_input(pin, PORT_GPIO_PULLUP) != DRIVER_OK) {
        LOG_E(REED_TAG, "gpio init failed");
        return (int)PORT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I(REED_TAG, "init ok");
    return DRIVER_OK;
}

int reed_deinit(reed_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->pin != 0U) {
        (void)driver_gpio_deinit(handle->pin);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int reed_read(reed_handle_t *handle, bool *closed)
{
    int level;
    int ret;

    if (closed == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = reed_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    level = driver_gpio_read(handle->pin);
    if (level < 0) {
        LOG_E(REED_TAG, "gpio read failed");
        return (int)PORT_IO_ERROR;
    }

    *closed = (level == (int)REED_ACTIVE_LEVEL);
    return DRIVER_OK;
}
