#include "sht2x.h"
#include "sht2x_register.h"
#include "iot_logger.h"

static int sht2x_measure(sht2x_handle_t *handle, uint8_t command,
                         uint32_t delay_ms, uint16_t *raw)
{
    uint8_t data[3];
    uint16_t value;
    int ret;

    ret = driver_i2c_write(&handle->i2c, &command, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 命令式器件：等待转换完成后再读，否则读到旧数据或 NACK */
    driver_delay_ms(delay_ms);

    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (handle->crc_enabled &&
        driver_crc8(data, 2U, SHT2X_CRC_POLY, SHT2X_CRC_INIT) != data[2]) {
        LOG_E("sht2x", "crc error");
        return (int)PORT_CRC_ERROR;
    }

    value = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    if ((value & SHT2X_STATUS_VDDS_LOW) != 0U) {
        LOG_W("sht2x", "vdd below 2.25V");
    }
    *raw = (uint16_t)(value & SHT2X_STATUS_MASK);
    return DRIVER_OK;
}

int sht2x_read_user_register(sht2x_handle_t *handle, uint8_t *value)
{
    uint8_t command = SHT2X_CMD_READ_USER;
    int ret;

    if (handle == NULL || value == NULL || handle->i2c.handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = driver_i2c_write(&handle->i2c, &command, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    return driver_i2c_read(&handle->i2c, value, 1U);
}

int sht2x_soft_reset(sht2x_handle_t *handle)
{
    uint8_t command = SHT2X_CMD_SOFT_RESET;
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = driver_i2c_write(&handle->i2c, &command, 1U);
    driver_delay_ms(SHT2X_RESET_DELAY_MS);
    return ret;
}

int sht2x_init(sht2x_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint8_t user = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SHT2X_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->crc_enabled = true;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("sht2x", "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    (void)sht2x_soft_reset(handle);
    handle->initialized = true;

    /* 复位后用户寄存器应为默认 0x02（bit1 恒为 1），据此确认器件类型 */
    if (sht2x_read_user_register(handle, &user) != DRIVER_OK) {
        handle->initialized = false;
        LOG_E("sht2x", "read user register failed");
        return (int)PORT_IO_ERROR;
    }
    if (user != SHT2X_USER_REG_DEFAULT) {
        LOG_W("sht2x", "unexpected user reg 0x%02X", user);
    }

    LOG_I("sht2x", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int sht2x_deinit(sht2x_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int sht2x_read(sht2x_handle_t *handle, float *temperature, float *humidity)
{
    uint16_t raw_t = 0U;
    uint16_t raw_h = 0U;
    float value;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (temperature == NULL && humidity == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    if (temperature != NULL) {
        ret = sht2x_measure(handle, SHT2X_CMD_MEASURE_T,
                            SHT2X_TEMP_DELAY_MS, &raw_t);
        if (ret != DRIVER_OK) {
            return ret;
        }
        *temperature = SHT2X_TEMP_OFFSET +
                        SHT2X_TEMP_SCALE * (float)raw_t / SHT2X_RAW_MAX;
    }

    if (humidity != NULL) {
        ret = sht2x_measure(handle, SHT2X_CMD_MEASURE_RH,
                            SHT2X_HUMIDITY_DELAY_MS, &raw_h);
        if (ret != DRIVER_OK) {
            return ret;
        }
        value = SHT2X_HUMIDITY_OFFSET +
                SHT2X_HUMIDITY_SCALE * (float)raw_h / SHT2X_RAW_MAX;
        if (value < 0.0f) {
            value = 0.0f;
        } else if (value > 100.0f) {
            value = 100.0f;
        }
        *humidity = value;
    }

    return DRIVER_OK;
}
