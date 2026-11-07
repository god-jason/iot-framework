#include "hdc2080.h"
#include "hdc2080_register.h"
#include "log.h"

static int hdc2080_read_le16(hdc2080_handle_t *handle, uint8_t reg_lsb, uint16_t *value)
{
    uint8_t data[2];
    int ret;

    ret = driver_i2c_read_reg(&handle->i2c, reg_lsb, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }
    /* 器件为低字节在前 */
    *value = (uint16_t)(((uint16_t)data[1] << 8) | (uint16_t)data[0]);
    return DRIVER_OK;
}

int hdc2080_read_id(hdc2080_handle_t *handle, uint16_t *id)
{
    if (handle == NULL || id == NULL || handle->i2c.handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    return hdc2080_read_le16(handle, HDC2080_REG_DEVICE_ID_L, id);
}

int hdc2080_init(hdc2080_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint8_t config;
    uint16_t id = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : HDC2080_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->heater_enabled = false;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("hdc2080", "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    /* 软复位（自清零位），复位后需等待 15ms */
    config = HDC2080_CONFIG_SOFT_RESET;
    (void)driver_i2c_write_reg(&handle->i2c, HDC2080_REG_CONFIG, &config, 1U);
    driver_delay_ms(HDC2080_RESET_DELAY_MS);

    handle->initialized = true;

    ret = hdc2080_read_id(handle, &id);
    if (ret != DRIVER_OK) {
        handle->initialized = false;
        LOG_E("hdc2080", "read id failed");
        return (int)PORT_IO_ERROR;
    }
    if (id != HDC2080_DEVICE_ID) {
        handle->initialized = false;
        LOG_E("hdc2080", "unexpected id 0x%04X", id);
        return (int)PORT_NOT_FOUND;
    }

    /* 温度 + 湿度顺序测量，14 位分辨率 */
    config = HDC2080_MEAS_CONFIG_14BIT;
    ret = driver_i2c_write_reg(&handle->i2c, HDC2080_REG_MEASUREMENT_CONFIG, &config, 1U);
    if (ret != DRIVER_OK) {
        handle->initialized = false;
        return ret;
    }

    LOG_I("hdc2080", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int hdc2080_deinit(hdc2080_handle_t *handle)
{
    uint8_t config;

    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->i2c.handle != NULL) {
        config = 0x00U;   /* 关闭加热器、清除触发位 */
        (void)driver_i2c_write_reg(&handle->i2c, HDC2080_REG_CONFIG, &config, 1U);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int hdc2080_read(hdc2080_handle_t *handle, float *temperature, float *humidity)
{
    uint8_t data[4];
    uint8_t config;
    uint16_t raw_t;
    uint16_t raw_h;
    float value;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (temperature == NULL && humidity == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    config = HDC2080_CONFIG_MEAS_TRIG;
    ret = driver_i2c_write_reg(&handle->i2c, HDC2080_REG_CONFIG, &config, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 测量完成前不能读取，否则得到的是上一次的结果 */
    driver_delay_ms(HDC2080_CONVERT_DELAY_MS);

    ret = driver_i2c_read_reg(&handle->i2c, HDC2080_REG_TEMP_LSB, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    raw_t = (uint16_t)(((uint16_t)data[1] << 8) | (uint16_t)data[0]);
    raw_h = (uint16_t)(((uint16_t)data[3] << 8) | (uint16_t)data[2]);

    if (temperature != NULL) {
        *temperature = HDC2080_TEMP_OFFSET +
                        HDC2080_TEMP_SCALE * (float)raw_t / HDC2080_RAW_MAX;
    }
    if (humidity != NULL) {
        value = HDC2080_HUMIDITY_SCALE * (float)raw_h / HDC2080_RAW_MAX;
        if (value > 100.0f) {
            value = 100.0f;
        }
        *humidity = value;
    }

    return DRIVER_OK;
}
