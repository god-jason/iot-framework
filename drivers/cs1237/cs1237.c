#include "cs1237.h"
#include "cs1237_register.h"
#include "log.h"

static void cs1237_sclk(cs1237_handle_t *handle, hal_gpio_level_t level)
{
    (void)driver_gpio_write(handle->sclk, level);
}

/* 一个完整的 SCLK 时钟脉冲 */
static void cs1237_pulse(cs1237_handle_t *handle)
{
    cs1237_sclk(handle, HAL_GPIO_HIGH);
    driver_delay_us(CS1237_SCLK_HIGH_US);
    cs1237_sclk(handle, HAL_GPIO_LOW);
    driver_delay_us(CS1237_SCLK_LOW_US);
}

static int cs1237_wait_ready(cs1237_handle_t *handle, uint32_t timeout_ms)
{
    uint32_t start = driver_tick_ms();

    for (;;) {
        int level = driver_gpio_read(handle->dout);

        if (level < 0) {
            return (int)HAL_IO_ERROR;
        }
        if (level == 0) {
            return DRIVER_OK;   /* DRDY 低表示转换完成 */
        }
        if ((uint32_t)(driver_tick_ms() - start) > timeout_ms) {
            return (int)HAL_TIMEOUT;
        }
        driver_delay_us(100U);
    }
}

int cs1237_init(cs1237_handle_t *handle, hal_pin_t sclk, hal_pin_t dout)
{
    if (handle == NULL || sclk == 0U || dout == 0U || sclk == dout) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->sclk = sclk;
    handle->dout = dout;
    handle->gain = CS1237_GAIN_1;
    handle->rate = CS1237_RATE_10HZ;
    handle->offset = CS1237_DEFAULT_OFFSET;
    handle->scale = CS1237_DEFAULT_SCALE;
    handle->initialized = false;

    if (driver_gpio_output(sclk, HAL_GPIO_LOW) != DRIVER_OK ||
        driver_gpio_input(dout, HAL_GPIO_PULLUP) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(CS1237_POWER_ON_MS);

    handle->initialized = true;
    LOG_I("cs1237", "init ok: sclk=%u dout=%u",
          (unsigned)HAL_PIN_GET_INDEX(sclk), (unsigned)HAL_PIN_GET_INDEX(dout));
    return DRIVER_OK;
}

int cs1237_deinit(cs1237_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        cs1237_sclk(handle, HAL_GPIO_LOW);
        (void)hal_pin_deinit(handle->sclk);
        (void)hal_pin_deinit(handle->dout);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int cs1237_is_ready(cs1237_handle_t *handle)
{
    int level;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    level = driver_gpio_read(handle->dout);
    if (level < 0) {
        return (int)HAL_IO_ERROR;
    }
    return (level == 0) ? 1 : 0;
}

int cs1237_read_raw(cs1237_handle_t *handle, int32_t *raw)
{
    uint32_t value = 0U;
    uint8_t i;
    int ret;

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = cs1237_wait_ready(handle, CS1237_READY_TIMEOUT_MS);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 24 个时钟读出转换结果，MSB 在前 */
    for (i = 0U; i < CS1237_DATA_BITS; i++) {
        int level;

        cs1237_sclk(handle, HAL_GPIO_HIGH);
        driver_delay_us(CS1237_SCLK_HIGH_US);
        level = driver_gpio_read(handle->dout);
        if (level < 0) {
            cs1237_sclk(handle, HAL_GPIO_LOW);
            return (int)HAL_IO_ERROR;
        }
        value = (value << 1) | ((level != 0) ? 1UL : 0UL);
        cs1237_sclk(handle, HAL_GPIO_LOW);
        driver_delay_us(CS1237_SCLK_LOW_US);
    }

    if ((value & CS1237_DATA_SIGN_BIT) != 0U) {
        value |= CS1237_DATA_SIGN_EXT;
    }
    *raw = (int32_t)value;
    return DRIVER_OK;
}

int cs1237_read_average(cs1237_handle_t *handle, uint8_t times, int32_t *raw)
{
    int64_t sum = 0;
    uint8_t i;

    if (handle == NULL || raw == NULL || !handle->initialized || times == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    for (i = 0U; i < times; i++) {
        int32_t value = 0;
        int ret = cs1237_read_raw(handle, &value);

        if (ret != DRIVER_OK) {
            return ret;
        }
        sum += value;
    }
    *raw = (int32_t)(sum / (int32_t)times);
    return DRIVER_OK;
}

int cs1237_set_gain(cs1237_handle_t *handle, cs1237_gain_t gain)
{
    if (handle == NULL || !handle->initialized || (uint16_t)gain > (uint16_t)CS1237_GAIN_128) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->gain = gain;
    return DRIVER_OK;
}

int cs1237_set_rate(cs1237_handle_t *handle, cs1237_rate_t rate)
{
    if (handle == NULL || !handle->initialized || (uint16_t)rate > (uint16_t)CS1237_RATE_1280HZ) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->rate = rate;
    return DRIVER_OK;
}

/* 配置帧：24 个时钟读走当前数据后，DOUT 反转为输出，送 4 位配置并锁存。
 * 需按 datasheet 补充：配置帧时钟数量、位序与锁存时刻各版本描述不一致。 */
int cs1237_write_config(cs1237_handle_t *handle)
{
    uint8_t config;
    uint8_t i;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    config = (uint8_t)(((uint8_t)handle->gain << CS1237_CFG_GAIN_SHIFT) |
                       ((uint8_t)handle->rate << CS1237_CFG_RATE_SHIFT));

    for (i = 0U; i < CS1237_DATA_BITS; i++) {
        cs1237_pulse(handle);
    }
    if (driver_gpio_output(handle->dout, HAL_GPIO_LOW) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    for (i = 0U; i < 4U; i++) {
        uint8_t bit = (uint8_t)((config >> (3U - i)) & 0x01U);

        (void)driver_gpio_write(handle->dout,
                               (bit != 0U) ? HAL_GPIO_HIGH : HAL_GPIO_LOW);
        cs1237_pulse(handle);
    }
    (void)driver_gpio_write(handle->dout, HAL_GPIO_LOW);
    cs1237_pulse(handle);

    (void)driver_gpio_input(handle->dout, HAL_GPIO_PULLUP);
    return DRIVER_OK;
}

int cs1237_set_offset(cs1237_handle_t *handle, int32_t offset)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->offset = offset;
    return DRIVER_OK;
}

int cs1237_set_scale(cs1237_handle_t *handle, float scale)
{
    if (handle == NULL || !handle->initialized || scale == 0.0f) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->scale = scale;
    return DRIVER_OK;
}

int cs1237_tare(cs1237_handle_t *handle, uint8_t times)
{
    int32_t raw = 0;
    int ret;

    if (handle == NULL || !handle->initialized || times == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = cs1237_read_average(handle, times, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    handle->offset = raw;
    return DRIVER_OK;
}

int cs1237_read(cs1237_handle_t *handle, float *weight)
{
    int32_t raw = 0;
    int ret;

    if (handle == NULL || weight == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = cs1237_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *weight = (float)(raw - handle->offset) / handle->scale;
    return DRIVER_OK;
}
