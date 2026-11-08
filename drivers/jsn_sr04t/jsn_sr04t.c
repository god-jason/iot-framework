#include "jsn_sr04t.h"
#include "log.h"

#define JSN_SR04T_TAG "jsn_sr04t"

static int jsn_sr04t_check(const jsn_sr04t_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return DRIVER_OK;
}

/* 等待 ECHO 到达指定电平；超时返回 HAL_TIMEOUT，同时回传已等待时长 */
static int jsn_sr04t_wait_level(const jsn_sr04t_handle_t *handle, int level,
                                uint32_t timeout_us, uint32_t *waited_us)
{
    uint32_t waited = 0U;

    for (;;) {
        int value = driver_gpio_read(handle->echo);

        if (value < 0) {
            return (int)HAL_IO_ERROR;
        }
        if (value == level) {
            if (waited_us != NULL) {
                *waited_us = waited;
            }
            return DRIVER_OK;
        }
        if (waited >= timeout_us) {
            return (int)HAL_TIMEOUT;
        }
        driver_delay_us(1U);
        waited++;
    }
}

int jsn_sr04t_init(jsn_sr04t_handle_t *handle, hal_pin_t trig, hal_pin_t echo)
{
    if (handle == NULL || trig == 0U || echo == 0U || trig == echo) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->trig = trig;
    handle->echo = echo;
    handle->timeout_us = JSN_SR04T_TIMEOUT_US;
    handle->initialized = false;

    if (driver_gpio_output(trig, HAL_GPIO_LOW) != DRIVER_OK) {
        LOG_E(JSN_SR04T_TAG, "trig gpio init failed");
        return (int)HAL_IO_ERROR;
    }
    if (driver_gpio_input(echo, HAL_GPIO_PULLDOWN) != DRIVER_OK) {
        LOG_E(JSN_SR04T_TAG, "echo gpio init failed");
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I(JSN_SR04T_TAG, "init ok");
    return DRIVER_OK;
}

int jsn_sr04t_deinit(jsn_sr04t_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->trig != 0U) {
        (void)driver_gpio_write(handle->trig, HAL_GPIO_LOW);
        (void)driver_gpio_deinit(handle->trig);
    }
    if (handle->echo != 0U) {
        (void)driver_gpio_deinit(handle->echo);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int jsn_sr04t_set_timeout(jsn_sr04t_handle_t *handle, uint32_t timeout_us)
{
    int ret = jsn_sr04t_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (timeout_us < 100U) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->timeout_us = timeout_us;
    return DRIVER_OK;
}

int jsn_sr04t_read_us(jsn_sr04t_handle_t *handle, uint32_t *echo_us)
{
    uint32_t high_us = 0U;
    int ret;

    if (echo_us == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = jsn_sr04t_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 上一次回响尚未结束，避免叠加测量 */
    ret = driver_gpio_read(handle->echo);
    if (ret < 0) {
        return (int)HAL_IO_ERROR;
    }
    if (ret != 0) {
        return (int)HAL_BUSY;
    }

    /* 触发脉冲：先确保空闲低电平，再拉高 10us */
    if (driver_gpio_write(handle->trig, HAL_GPIO_LOW) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_us(JSN_SR04T_TRIG_IDLE_US);
    if (driver_gpio_write(handle->trig, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_us(JSN_SR04T_TRIG_PULSE_US);
    if (driver_gpio_write(handle->trig, HAL_GPIO_LOW) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 等待回响上升沿 */
    ret = jsn_sr04t_wait_level(handle, 1, handle->timeout_us, NULL);
    if (ret != DRIVER_OK) {
        LOG_D(JSN_SR04T_TAG, "no rising edge");
        return ret;
    }

    /* 测量高电平持续时间 */
    ret = jsn_sr04t_wait_level(handle, 0, handle->timeout_us, &high_us);
    if (ret != DRIVER_OK) {
        LOG_W(JSN_SR04T_TAG, "echo stuck high");
        return ret;
    }

    *echo_us = high_us;
    return DRIVER_OK;
}

int jsn_sr04t_read(jsn_sr04t_handle_t *handle, float *distance_m)
{
    uint32_t echo_us = 0U;
    int ret;

    if (distance_m == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = jsn_sr04t_read_us(handle, &echo_us);
    if (ret != DRIVER_OK) {
        return ret;
    }

    *distance_m = (float)echo_us / (JSN_SR04T_US_PER_CM * 100.0f);
    if (*distance_m > JSN_SR04T_MAX_RANGE_M) {
        return (int)HAL_OVERFLOW;
    }
    return DRIVER_OK;
}
