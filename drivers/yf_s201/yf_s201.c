#include "yf_s201.h"
#include "log.h"

#define YF_S201_TAG "yf_s201"

static int yf_s201_check(const yf_s201_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int yf_s201_init(yf_s201_handle_t *handle, iot_pin_t pin)
{
    if (handle == NULL || pin == 0U) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->pin = pin;
    handle->pulses_per_liter = YF_S201_PULSES_PER_LITER_DEFAULT;
    handle->window_ms = YF_S201_WINDOW_MS_DEFAULT;
    handle->initialized = false;

    /* 霍尔输出为开集结构，需上拉；无水流时读到稳定高电平（不产生上升沿） */
    if (driver_gpio_input(pin, IOT_GPIO_PULLUP) != DRIVER_OK) {
        LOG_E(YF_S201_TAG, "gpio init failed");
        return (int)IOT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I(YF_S201_TAG, "init ok, %u pulses/L",
          (unsigned int)YF_S201_PULSES_PER_LITER_DEFAULT);
    return DRIVER_OK;
}

int yf_s201_deinit(yf_s201_handle_t *handle)
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

int yf_s201_set_pulses_per_liter(yf_s201_handle_t *handle, float pulses_per_liter)
{
    int ret = yf_s201_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (pulses_per_liter < 100.0f || pulses_per_liter > 2000.0f) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->pulses_per_liter = pulses_per_liter;
    return DRIVER_OK;
}

int yf_s201_set_window(yf_s201_handle_t *handle, uint32_t window_ms)
{
    int ret = yf_s201_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (window_ms < YF_S201_WINDOW_MS_MIN || window_ms > YF_S201_WINDOW_MS_MAX) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->window_ms = window_ms;
    return DRIVER_OK;
}

int yf_s201_read(yf_s201_handle_t *handle, float *hz, float *liters_per_min)
{
    uint32_t window_us;
    uint32_t elapsed_us = 0U;
    uint32_t pulses = 0U;
    int last;
    int ret;

    if (hz == NULL && liters_per_min == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = yf_s201_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    window_us = handle->window_ms * 1000U;

    last = driver_gpio_read(handle->pin);
    if (last < 0) {
        return (int)IOT_IO_ERROR;
    }

    while (elapsed_us < window_us) {
        int cur;

        driver_delay_us(YF_S201_SAMPLE_US);
        elapsed_us += YF_S201_SAMPLE_US;

        cur = driver_gpio_read(handle->pin);
        if (cur < 0) {
            return (int)IOT_IO_ERROR;
        }
        if (cur != 0 && last == 0) {
            pulses++; /* 上升沿计数 */
        }
        last = cur;
    }

    if (pulses == 0U) {
        /* 无脉冲：静水，频率与流量均为 0（非错误状态） */
        if (hz != NULL) {
            *hz = 0.0f;
        }
        if (liters_per_min != NULL) {
            *liters_per_min = 0.0f;
        }
        return DRIVER_OK;
    }

    {
        float freq = (float)pulses * 1000000.0f / (float)window_us;

        if (hz != NULL) {
            *hz = freq;
        }
        if (liters_per_min != NULL) {
            *liters_per_min = freq * 60.0f / handle->pulses_per_liter;
        }
    }

    return DRIVER_OK;
}
