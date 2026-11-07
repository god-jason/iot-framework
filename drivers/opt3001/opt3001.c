#include "opt3001.h"
#include "opt3001_register.h"
#include "log.h"

static uint32_t opt3001_conv_delay_ms(opt3001_conv_time_t conv_time)
{
    return (conv_time == OPT3001_CONV_800MS) ? OPT3001_DELAY_800MS : OPT3001_DELAY_100MS;
}

static uint16_t opt3001_build_config(const opt3001_handle_t *handle)
{
    uint16_t config = OPT3001_CONFIG_RN;
    uint16_t ct;

    ct = (handle->conv_time == OPT3001_CONV_800MS) ? OPT3001_CT_800MS : OPT3001_CT_100MS;
    config |= (uint16_t)(ct << OPT3001_CONFIG_CT_SHIFT);
    config |= handle->continuous ? OPT3001_CONFIG_M_CONTINUOUS : OPT3001_CONFIG_M_SINGLE;
    config |= OPT3001_CONFIG_L;
    return config;
}

static int opt3001_write_config(opt3001_handle_t *handle, uint16_t config)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(config >> 8);
    buf[1] = (uint8_t)(config & 0xFFU);
    return driver_i2c_write_reg(&handle->i2c, OPT3001_REG_CONFIG, buf, sizeof(buf));
}

static int opt3001_read_u16(opt3001_handle_t *handle, uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    int ret;

    ret = driver_i2c_read_reg(&handle->i2c, reg, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }
    *value = (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
    return DRIVER_OK;
}

int opt3001_init(opt3001_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint16_t device_id = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : OPT3001_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->conv_time = OPT3001_CONV_100MS;
    handle->continuous = true;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("opt3001", "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    ret = opt3001_read_u16(handle, OPT3001_REG_DEVICE_ID, &device_id);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (device_id != OPT3001_DEVICE_ID) {
        LOG_E("opt3001", "unexpected device id 0x%04X", device_id);
        return (int)IOT_NOT_FOUND;
    }

    driver_delay_ms(OPT3001_POWER_ON_DELAY_MS);
    ret = opt3001_write_config(handle, opt3001_build_config(handle));
    if (ret != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(opt3001_conv_delay_ms(handle->conv_time));

    handle->initialized = true;
    LOG_I("opt3001", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int opt3001_deinit(opt3001_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->i2c.handle != NULL) {
        (void)opt3001_write_config(handle, OPT3001_CONFIG_M_SHUTDOWN);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int opt3001_set_conversion_time(opt3001_handle_t *handle, opt3001_conv_time_t conv_time)
{
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (conv_time != OPT3001_CONV_100MS && conv_time != OPT3001_CONV_800MS) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->conv_time = conv_time;
    ret = opt3001_write_config(handle, opt3001_build_config(handle));
    if (ret != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(opt3001_conv_delay_ms(handle->conv_time));
    return DRIVER_OK;
}

int opt3001_set_continuous(opt3001_handle_t *handle, bool enable)
{
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->continuous = enable;
    ret = opt3001_write_config(handle, opt3001_build_config(handle));
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)IOT_IO_ERROR;
}

int opt3001_read_raw(opt3001_handle_t *handle, uint16_t *raw, float *lux)
{
    uint16_t value = 0U;
    uint16_t mantissa;
    uint8_t exponent;
    uint8_t i;
    float scale;
    int ret;

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    /* 单次模式需重新触发一次转换；连续模式直接等待新数据 */
    if (!handle->continuous) {
        ret = opt3001_write_config(handle, opt3001_build_config(handle));
        if (ret != DRIVER_OK) {
            return (int)IOT_IO_ERROR;
        }
    }
    driver_delay_ms(opt3001_conv_delay_ms(handle->conv_time));

    ret = opt3001_read_u16(handle, OPT3001_REG_RESULT, &value);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *raw = value;

    if (lux != NULL) {
        mantissa = OPT3001_RESULT_MANTISSA(value);
        exponent = OPT3001_RESULT_EXPONENT(value);
        scale = OPT3001_LUX_LSB;
        for (i = 0U; i < exponent; i++) {
            scale *= 2.0f;
        }
        *lux = (float)mantissa * scale;
    }
    return DRIVER_OK;
}

int opt3001_read(opt3001_handle_t *handle, float *lux)
{
    uint16_t raw = 0U;

    if (lux == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    return opt3001_read_raw(handle, &raw, lux);
}

int opt3001_get_device_id(opt3001_handle_t *handle, uint16_t *device_id)
{
    if (handle == NULL || device_id == NULL || handle->i2c.handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    return opt3001_read_u16(handle, OPT3001_REG_DEVICE_ID, device_id);
}
