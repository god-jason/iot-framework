#include "tsl2561.h"
#include "tsl2561_register.h"
#include "log.h"

#include <math.h>

static uint8_t tsl2561_timing_value(const tsl2561_handle_t *handle)
{
    uint8_t value = (handle->gain == TSL2561_GAIN_16X) ? TSL2561_TIMING_GAIN_16X : 0x00U;

    switch (handle->integration) {
    case TSL2561_INTEG_13MS:
        value |= TSL2561_TIMING_INTEG_13MS;
        break;
    case TSL2561_INTEG_101MS:
        value |= TSL2561_TIMING_INTEG_101MS;
        break;
    case TSL2561_INTEG_402MS:
    default:
        value |= TSL2561_TIMING_INTEG_402MS;
        break;
    }
    return value;
}

static float tsl2561_integration_time_ms(tsl2561_integration_t integration)
{
    switch (integration) {
    case TSL2561_INTEG_13MS:
        return TSL2561_INTEG_13MS_TIME;
    case TSL2561_INTEG_101MS:
        return TSL2561_INTEG_101MS_TIME;
    case TSL2561_INTEG_402MS:
    default:
        return TSL2561_INTEG_402MS_TIME;
    }
}

static uint32_t tsl2561_integration_delay_ms(tsl2561_integration_t integration)
{
    switch (integration) {
    case TSL2561_INTEG_13MS:
        return TSL2561_DELAY_13MS;
    case TSL2561_INTEG_101MS:
        return TSL2561_DELAY_101MS;
    case TSL2561_INTEG_402MS:
    default:
        return TSL2561_DELAY_402MS;
    }
}

static int tsl2561_write_timing(tsl2561_handle_t *handle)
{
    uint8_t value = tsl2561_timing_value(handle);

    return driver_i2c_write_reg(&handle->i2c, (uint8_t)(TSL2561_CMD | TSL2561_REG_TIMING),
                                &value, 1U);
}

static int tsl2561_write_power(tsl2561_handle_t *handle, uint8_t state)
{
    return driver_i2c_write_reg(&handle->i2c, (uint8_t)(TSL2561_CMD | TSL2561_REG_CONTROL),
                                &state, 1U);
}

/* 把原始计数归一化到 402ms / 1x 基准，便于统一套用 datasheet 系数 */
static float tsl2561_normalize(uint32_t count, const tsl2561_handle_t *handle)
{
    float scale = TSL2561_INTEG_402MS_TIME / tsl2561_integration_time_ms(handle->integration);

    if (handle->gain == TSL2561_GAIN_16X) {
        scale /= 16.0f;
    }
    return (float)count * scale;
}

/* lux 系数取自 datasheet 中 T/FN/CL 封装的分段经验公式，实际需按标定修正 */
static float tsl2561_lux_from_counts(float ch0, float ch1)
{
    float ratio;
    float lux;

    if (ch0 <= 0.0f) {
        return 0.0f;
    }

    ratio = ch1 / ch0;
    if (ratio <= TSL2561_LUX_RATIO_1) {
        lux = (TSL2561_LUX_K1A * ch0) - (TSL2561_LUX_K1B * ch0 * powf(ratio, 1.4f));
    } else if (ratio <= TSL2561_LUX_RATIO_2) {
        lux = (TSL2561_LUX_K2A * ch0) - (TSL2561_LUX_K2B * ch1);
    } else if (ratio <= TSL2561_LUX_RATIO_3) {
        lux = (TSL2561_LUX_K3A * ch0) - (TSL2561_LUX_K3B * ch1);
    } else if (ratio <= TSL2561_LUX_RATIO_4) {
        lux = (TSL2561_LUX_K4A * ch0) - (TSL2561_LUX_K4B * ch1);
    } else {
        lux = 0.0f;
    }
    return (lux < 0.0f) ? 0.0f : lux;
}

int tsl2561_init(tsl2561_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : TSL2561_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->gain = TSL2561_GAIN_1X;
    handle->integration = TSL2561_INTEG_402MS;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("tsl2561", "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    ret = driver_i2c_read_reg(&handle->i2c, (uint8_t)(TSL2561_CMD | TSL2561_REG_ID), &id, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if ((id & TSL2561_ID_PART_MASK) != TSL2561_ID_PART_TSL2560 &&
        (id & TSL2561_ID_PART_MASK) != TSL2561_ID_PART_TSL2561) {
        LOG_E("tsl2561", "unexpected id 0x%02X", id);
        return (int)IOT_NOT_FOUND;
    }

    if (tsl2561_write_power(handle, TSL2561_CONTROL_POWER_ON) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(1U);

    if (tsl2561_write_timing(handle) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("tsl2561", "init ok at 0x%02X (id=0x%02X)", handle->i2c.address, id);
    return DRIVER_OK;
}

int tsl2561_deinit(tsl2561_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->i2c.handle != NULL) {
        (void)tsl2561_write_power(handle, TSL2561_CONTROL_POWER_OFF);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int tsl2561_set_gain(tsl2561_handle_t *handle, tsl2561_gain_t gain)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (gain != TSL2561_GAIN_1X && gain != TSL2561_GAIN_16X) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->gain = gain;
    if (tsl2561_write_timing(handle) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(tsl2561_integration_delay_ms(handle->integration));
    return DRIVER_OK;
}

int tsl2561_set_integration_time(tsl2561_handle_t *handle, tsl2561_integration_t integration)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (integration != TSL2561_INTEG_13MS && integration != TSL2561_INTEG_101MS &&
        integration != TSL2561_INTEG_402MS) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->integration = integration;
    if (tsl2561_write_timing(handle) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(tsl2561_integration_delay_ms(handle->integration));
    return DRIVER_OK;
}

int tsl2561_read_raw(tsl2561_handle_t *handle, uint32_t *ch0, uint32_t *ch1)
{
    uint8_t buf[4] = { 0U, 0U, 0U, 0U };
    uint8_t cmd;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    /* 积分时间 + 余量后读取，datasheet 要求等待一个完整积分周期 */
    driver_delay_ms(tsl2561_integration_delay_ms(handle->integration));

    cmd = (uint8_t)(TSL2561_CMD | TSL2561_CMD_BLOCK | TSL2561_REG_DATA0LOW);
    ret = driver_i2c_read_reg(&handle->i2c, cmd, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* DATA0/DATA1 为小端：低字节在前 */
    if (ch0 != NULL) {
        *ch0 = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8);
    }
    if (ch1 != NULL) {
        *ch1 = (uint32_t)buf[2] | ((uint32_t)buf[3] << 8);
    }
    return DRIVER_OK;
}

int tsl2561_read(tsl2561_handle_t *handle, tsl2561_data_t *data)
{
    uint32_t ch0 = 0U;
    uint32_t ch1 = 0U;
    int ret;

    if (handle == NULL || data == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = tsl2561_read_raw(handle, &ch0, &ch1);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->ch0 = ch0;
    data->ch1 = ch1;
    data->saturated = (ch0 >= TSL2561_MAX_COUNT) || (ch1 >= TSL2561_MAX_COUNT);
    data->lux = tsl2561_lux_from_counts(tsl2561_normalize(ch0, handle),
                                        tsl2561_normalize(ch1, handle));
    return DRIVER_OK;
}
