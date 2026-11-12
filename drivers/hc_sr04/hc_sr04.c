#include "hc_sr04.h"
#include "log.h"

#define HC_SR04_TAG "hc_sr04"

static int hc_sr04_check(const hc_sr04_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return HAL_OK;
}

/* 等待 ECHO 到达指定电平，超时返回 HAL_TIMEOUT；等待期间按 1us 递增计时 */
static int hc_sr04_wait_level(const hc_sr04_handle_t *handle, int level,
                             uint32_t timeout_us, uint32_t *waited_us)
{
    uint32_t waited = 0U;

    for (;;) {
        int value = hal_gpio_read_level(handle->echo);

        if (value < 0) {
            return (int)HAL_IO_ERROR;
        }
        if (value == level) {
            if (waited_us != NULL) {
                *waited_us = waited;
            }
            return HAL_OK;
        }
        if (waited >= timeout_us) {
            return (int)HAL_TIMEOUT;
        }
        hal_delay_us(1U);
        waited++;
    }
}

int hc_sr04_init(hc_sr04_handle_t *handle, hal_pin_t trig, hal_pin_t echo)
{
    if (handle == NULL || trig == 0U || echo == 0U || trig == echo) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->trig = trig;
    handle->echo = echo;
    handle->timeout_us = HC_SR04_TIMEOUT_DEFAULT_US;
    handle->initialized = false;

    if (hal_gpio_config_output(trig, HAL_GPIO_LOW) != HAL_OK) {
        LOG_E(HC_SR04_TAG, "trig gpio init failed");
        return (int)HAL_IO_ERROR;
    }
    if (hal_gpio_config_input(echo, HAL_GPIO_PULLDOWN) != HAL_OK) {
        LOG_E(HC_SR04_TAG, "echo gpio init failed");
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I(HC_SR04_TAG, "init ok");
    return HAL_OK;
}

int hc_sr04_deinit(hc_sr04_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->trig != 0U) {
        (void)hal_gpio_write(handle->trig, HAL_GPIO_LOW);
    }
    handle->initialized = false;
    return HAL_OK;
}

int hc_sr04_set_timeout(hc_sr04_handle_t *handle, uint32_t timeout_us)
{
    int ret = hc_sr04_check(handle);

    if (ret != HAL_OK) {
        return ret;
    }
    if (timeout_us < 100U) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->timeout_us = timeout_us;
    return HAL_OK;
}

int hc_sr04_read_us(hc_sr04_handle_t *handle, uint32_t *echo_us)
{
    uint32_t high_us = 0U;
    int ret;

    if (echo_us == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = hc_sr04_check(handle);
    if (ret != HAL_OK) {
        return ret;
    }

    /* 上一次回响未结束，直接返回忙 */
    ret = hal_gpio_read_level(handle->echo);
    if (ret < 0) {
        return (int)HAL_IO_ERROR;
    }
    if (ret != 0) {
        return (int)HAL_BUSY;
    }

    /* 触发脉冲 */
    if (hal_gpio_write(handle->trig, HAL_GPIO_LOW) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    hal_delay_us(HC_SR04_TRIG_IDLE_US);
    if (hal_gpio_write(handle->trig, HAL_GPIO_HIGH) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    hal_delay_us(HC_SR04_TRIG_PULSE_US);
    if (hal_gpio_write(handle->trig, HAL_GPIO_LOW) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 等待回响上升沿 */
    ret = hc_sr04_wait_level(handle, 1, handle->timeout_us, NULL);
    if (ret != HAL_OK) {
        LOG_D(HC_SR04_TAG, "no rising edge");
        return ret;
    }

    /* 测量高电平持续时间 */
    ret = hc_sr04_wait_level(handle, 0, handle->timeout_us, &high_us);
    if (ret != HAL_OK) {
        LOG_W(HC_SR04_TAG, "echo stuck high");
        return ret;
    }

    *echo_us = high_us;
    return HAL_OK;
}

int hc_sr04_read(hc_sr04_handle_t *handle, float *distance_cm)
{
    uint32_t echo_us = 0U;
    int ret;

    if (distance_cm == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = hc_sr04_read_us(handle, &echo_us);
    if (ret != HAL_OK) {
        return ret;
    }

    *distance_cm = (float)echo_us / HC_SR04_US_PER_CM;
    if (*distance_cm > HC_SR04_MAX_RANGE_CM) {
        return (int)HAL_OVERFLOW;
    }
    return HAL_OK;
}

int hc_sr04_read_average(hc_sr04_handle_t *handle, uint8_t samples, float *distance_cm)
{
    float sum = 0.0f;
    uint8_t valid = 0U;
    uint8_t i;
    int ret;

    if (distance_cm == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = hc_sr04_check(handle);
    if (ret != HAL_OK) {
        return ret;
    }
    if (samples == 0U || samples > HC_SR04_AVG_MAX_SAMPLES) {
        return (int)HAL_INVALID_PARAM;
    }

    for (i = 0U; i < samples; i++) {
        float value = 0.0f;

        if (hc_sr04_read(handle, &value) == HAL_OK) {
            sum += value;
            valid++;
        }
        /* 相邻测量的回响余波需要间隔（datasheet 建议 >60ms） */
        hal_delay_ms(60U);
    }

    if (valid == 0U) {
        return (int)HAL_TIMEOUT;
    }
    *distance_cm = sum / (float)valid;
    return HAL_OK;
}
