#include "hx711.h"
#include "hx711_register.h"
#include "ros.h"
#include "common.h"

/* 通道/增益对应的总时钟数（24 数据位 + 选择脉冲） */
static uint8_t hx711_gain_pulses(hx711_gain_e gain)
{
    switch (gain) {
    case HX711_GAIN_B_32:
        return (uint8_t)HX711_PULSES_B_GAIN_32;
    case HX711_GAIN_A_64:
        return (uint8_t)HX711_PULSES_A_GAIN_64;
    case HX711_GAIN_A_128:
    default:
        return (uint8_t)HX711_PULSES_A_GAIN_128;
    }
}

static void hx711_sck(hx711_handle_t *handle, hal_gpio_level_e level)
{
    (void)hal_gpio_write(handle->sck, level);
}

static int hx711_wait_ready(hx711_handle_t *handle, uint32_t timeout_ms)
{
    uint32_t start = hal_tick_get();

    for (;;) {
        int level = hal_gpio_read_level(handle->dout);

        if (level < 0) {
            return (int)HAL_IO_ERROR;
        }
        if (level == 0) {
            return HAL_OK;   /* DOUT 拉低表示转换完成 */
        }
        if ((uint32_t)(hal_tick_get() - start) > timeout_ms) {
            return (int)HAL_TIMEOUT;
        }
        hal_delay_us(100U);
    }
}

int hx711_init(hx711_handle_t *handle, hal_pin_t sck, hal_pin_t dout)
{
    if (handle == NULL || sck == 0U || dout == 0U || sck == dout) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->sck = sck;
    handle->dout = dout;
    handle->gain = HX711_GAIN_A_128;
    handle->offset = HX711_DEFAULT_OFFSET;
    handle->scale = HX711_DEFAULT_SCALE;
    handle->initialized = false;

    if (hal_gpio_config_output(sck, HAL_GPIO_LOW) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (hal_gpio_config_input(dout, HAL_GPIO_PULLUP) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }

    hal_delay_ms(HX711_POWER_ON_MS);
    handle->initialized = true;
    LOG_I("hx711", "init ok: sck=%u dout=%u",
          (unsigned)HAL_PIN_GET_INDEX(sck), (unsigned)HAL_PIN_GET_INDEX(dout));
    return HAL_OK;
}

int hx711_deinit(hx711_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        hx711_sck(handle, HAL_GPIO_LOW);
        (void)hal_pin_deinit(handle->sck);
        (void)hal_pin_deinit(handle->dout);
    }
    handle->initialized = false;
    return HAL_OK;
}

int hx711_is_ready(hx711_handle_t *handle)
{
    int level;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    level = hal_gpio_read_level(handle->dout);
    if (level < 0) {
        return (int)HAL_IO_ERROR;
    }
    return (level == 0) ? 1 : 0;
}

int hx711_set_gain(hx711_handle_t *handle, hx711_gain_e gain)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (gain != HX711_GAIN_A_128 && gain != HX711_GAIN_A_64 && gain != HX711_GAIN_B_32) {
        return (int)HAL_INVALID_PARAM;
    }
    /* 增益在本次读取的尾部脉冲中生效 */
    handle->gain = gain;
    return HAL_OK;
}

int hx711_read_raw(hx711_handle_t *handle, int32_t *raw)
{
    uint32_t value = 0U;
    uint8_t pulses;
    uint8_t i;
    int ret;

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = hx711_wait_ready(handle, HX711_READY_TIMEOUT_MS);
    if (ret != HAL_OK) {
        return ret;
    }

    /* 24 个时钟读出转换结果，高位在前 */
    for (i = 0U; i < HX711_DATA_BITS; i++) {
        int level;

        hx711_sck(handle, HAL_GPIO_HIGH);
        hal_delay_us(HX711_SCK_HIGH_US);
        level = hal_gpio_read_level(handle->dout);
        if (level < 0) {
            hx711_sck(handle, HAL_GPIO_LOW);
            return (int)HAL_IO_ERROR;
        }
        value = (value << 1) | ((level != 0) ? 1UL : 0UL);
        hx711_sck(handle, HAL_GPIO_LOW);
        hal_delay_us(HX711_SCK_LOW_US);
    }

    /* 附加 1~3 个脉冲，选择下一次转换的通道与增益 */
    pulses = (uint8_t)(hx711_gain_pulses(handle->gain) - HX711_DATA_BITS);
    for (i = 0U; i < pulses; i++) {
        hx711_sck(handle, HAL_GPIO_HIGH);
        hal_delay_us(HX711_SCK_HIGH_US);
        hx711_sck(handle, HAL_GPIO_LOW);
        hal_delay_us(HX711_SCK_LOW_US);
    }

    /* 24 位补码扩展为 int32 */
    if ((value & HX711_DATA_SIGN_BIT) != 0U) {
        value |= HX711_DATA_SIGN_EXT;
    }
    *raw = (int32_t)value;
    return HAL_OK;
}

int hx711_read_average(hx711_handle_t *handle, uint8_t times, int32_t *raw)
{
    int64_t sum = 0;
    uint8_t i;

    if (handle == NULL || raw == NULL || !handle->initialized || times == 0U) {
        return (int)HAL_INVALID_PARAM;
    }

    for (i = 0U; i < times; i++) {
        int32_t value = 0;
        int ret = hx711_read_raw(handle, &value);

        if (ret != HAL_OK) {
            return ret;
        }
        sum += value;
    }
    *raw = (int32_t)(sum / (int32_t)times);
    return HAL_OK;
}

int hx711_set_offset(hx711_handle_t *handle, int32_t offset)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->offset = offset;
    return HAL_OK;
}

int hx711_set_scale(hx711_handle_t *handle, float scale)
{
    if (handle == NULL || !handle->initialized || scale == 0.0f) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->scale = scale;
    return HAL_OK;
}

int hx711_tare(hx711_handle_t *handle, uint8_t times)
{
    int32_t raw = 0;
    int ret;

    if (handle == NULL || !handle->initialized || times == 0U) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = hx711_read_average(handle, times, &raw);
    if (ret != HAL_OK) {
        return ret;
    }
    handle->offset = raw;
    return HAL_OK;
}

int hx711_read(hx711_handle_t *handle, float *weight)
{
    int32_t raw = 0;
    int ret;

    if (handle == NULL || weight == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = hx711_read_raw(handle, &raw);
    if (ret != HAL_OK) {
        return ret;
    }
    *weight = (float)(raw - handle->offset) / handle->scale;
    return HAL_OK;
}
