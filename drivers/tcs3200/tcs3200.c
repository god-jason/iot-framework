#include "tcs3200.h"
#include "log.h"

#define TCS3200_TAG "tcs3200"

static int tcs3200_check(const tcs3200_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return DRIVER_OK;
}

static int tcs3200_pins_valid(const tcs3200_handle_t *handle)
{
    hal_pin_t pins[5];
    uint8_t i;
    uint8_t j;

    pins[0] = handle->s0;
    pins[1] = handle->s1;
    pins[2] = handle->s2;
    pins[3] = handle->s3;
    pins[4] = handle->out;

    for (i = 0U; i < 5U; i++) {
        if (pins[i] == 0U) {
            return 0;
        }
        for (j = (uint8_t)(i + 1U); j < 5U; j++) {
            if (pins[i] == pins[j]) {
                return 0;
            }
        }
    }
    return 1;
}

/* filter 编码：bit1 = S2，bit0 = S3 */
static int tcs3200_select(const tcs3200_handle_t *handle, uint8_t filter)
{
    hal_gpio_level_e s2 = ((filter & 0x02U) != 0U) ? HAL_GPIO_HIGH : HAL_GPIO_LOW;
    hal_gpio_level_e s3 = ((filter & 0x01U) != 0U) ? HAL_GPIO_HIGH : HAL_GPIO_LOW;

    if (driver_gpio_write(handle->s2, s2) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (driver_gpio_write(handle->s3, s3) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_us(TCS3200_SETTLE_US);
    return DRIVER_OK;
}

/* 在 window_us 内统计 OUT 上升沿数量 */
static int tcs3200_count(const tcs3200_handle_t *handle, uint32_t *pulses)
{
    uint32_t elapsed_us = 0U;
    uint32_t count = 0U;
    int last;

    last = driver_gpio_read(handle->out);
    if (last < 0) {
        return (int)HAL_IO_ERROR;
    }

    while (elapsed_us < handle->window_us) {
        int cur;

        driver_delay_us(TCS3200_SAMPLE_US);
        elapsed_us += TCS3200_SAMPLE_US;

        cur = driver_gpio_read(handle->out);
        if (cur < 0) {
            return (int)HAL_IO_ERROR;
        }
        if (cur != 0 && last == 0) {
            count++;
        }
        last = cur;
    }

    *pulses = count;
    return DRIVER_OK;
}

static int tcs3200_measure(const tcs3200_handle_t *handle, uint8_t filter, uint32_t *hz)
{
    uint32_t pulses = 0U;
    int ret;

    ret = tcs3200_select(handle, filter);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = tcs3200_count(handle, &pulses);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* Hz = 脉冲数 / 窗口秒数，按 64 位中间量避免溢出 */
    *hz = (uint32_t)(((uint64_t)pulses * 1000000ULL) / (uint64_t)handle->window_us);
    return DRIVER_OK;
}

int tcs3200_init(tcs3200_handle_t *handle, hal_pin_t s0, hal_pin_t s1,
                 hal_pin_t s2, hal_pin_t s3, hal_pin_t out)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->s0 = s0;
    handle->s1 = s1;
    handle->s2 = s2;
    handle->s3 = s3;
    handle->out = out;
    handle->window_us = TCS3200_WINDOW_US;
    handle->initialized = false;

    if (!tcs3200_pins_valid(handle)) {
        return (int)HAL_INVALID_PARAM;
    }

    if (driver_gpio_output(s0, (TCS3200_SCALE_S0 != 0U) ? HAL_GPIO_HIGH : HAL_GPIO_LOW) != DRIVER_OK ||
        driver_gpio_output(s1, (TCS3200_SCALE_S1 != 0U) ? HAL_GPIO_HIGH : HAL_GPIO_LOW) != DRIVER_OK ||
        driver_gpio_output(s2, HAL_GPIO_LOW) != DRIVER_OK ||
        driver_gpio_output(s3, HAL_GPIO_LOW) != DRIVER_OK) {
        LOG_E(TCS3200_TAG, "gpio output init failed");
        return (int)HAL_IO_ERROR;
    }
    if (driver_gpio_input(out, HAL_GPIO_PULLDOWN) != DRIVER_OK) {
        LOG_E(TCS3200_TAG, "gpio input init failed");
        return (int)HAL_IO_ERROR;
    }

    /* 默认选择红色通道并等待滤波稳定 */
    if (tcs3200_select(handle, TCS3200_FILTER_RED) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I(TCS3200_TAG, "init ok, scale %u%%", (unsigned int)TCS3200_SCALE_PERCENT);
    return DRIVER_OK;
}

int tcs3200_deinit(tcs3200_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        /* S0=S1=0：输出掉电，降低功耗 */
        (void)driver_gpio_write(handle->s0, HAL_GPIO_LOW);
        (void)driver_gpio_write(handle->s1, HAL_GPIO_LOW);
        (void)driver_gpio_write(handle->s2, HAL_GPIO_LOW);
        (void)driver_gpio_write(handle->s3, HAL_GPIO_LOW);
    }
    (void)driver_gpio_deinit(handle->s0);
    (void)driver_gpio_deinit(handle->s1);
    (void)driver_gpio_deinit(handle->s2);
    (void)driver_gpio_deinit(handle->s3);
    (void)driver_gpio_deinit(handle->out);
    handle->initialized = false;
    return DRIVER_OK;
}

int tcs3200_read(tcs3200_handle_t *handle, tcs3200_color_t *color)
{
    uint32_t r_hz = 0U;
    uint32_t g_hz = 0U;
    uint32_t b_hz = 0U;
    uint32_t max_hz;
    int ret;

    if (color == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = tcs3200_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = tcs3200_measure(handle, TCS3200_FILTER_RED, &r_hz);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = tcs3200_measure(handle, TCS3200_FILTER_GREEN, &g_hz);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = tcs3200_measure(handle, TCS3200_FILTER_BLUE, &b_hz);
    if (ret != DRIVER_OK) {
        return ret;
    }

    color->r_hz = r_hz;
    color->g_hz = g_hz;
    color->b_hz = b_hz;

    max_hz = r_hz;
    if (g_hz > max_hz) {
        max_hz = g_hz;
    }
    if (b_hz > max_hz) {
        max_hz = b_hz;
    }

    if (max_hz == 0U) {
        /* 全黑或未对准：三分量置 0，避免除零 */
        color->r = 0.0f;
        color->g = 0.0f;
        color->b = 0.0f;
    } else {
        color->r = (float)r_hz / (float)max_hz;
        color->g = (float)g_hz / (float)max_hz;
        color->b = (float)b_hz / (float)max_hz;
    }

    return DRIVER_OK;
}
