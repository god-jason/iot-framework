#include "sht35.h"
#include "sht35_register.h"
#include "log.h"

static int sht35_write_cmd(sht35_handle_t *handle, uint16_t command)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

static uint16_t sht35_measure_command(sht35_handle_t *handle)
{
    switch (handle->repeatability) {
    case SHT35_REPEAT_MEDIUM:
        return SHT35_CMD_MEASURE_MEDIUM;
    case SHT35_REPEAT_LOW:
        return SHT35_CMD_MEASURE_LOW;
    case SHT35_REPEAT_HIGH:
    default:
        return SHT35_CMD_MEASURE_HIGH;
    }
}

static uint32_t sht35_measure_delay(sht35_handle_t *handle)
{
    switch (handle->repeatability) {
    case SHT35_REPEAT_MEDIUM:
        return SHT35_MEASURE_DELAY_MED_MS;
    case SHT35_REPEAT_LOW:
        return SHT35_MEASURE_DELAY_LOW_MS;
    case SHT35_REPEAT_HIGH:
    default:
        return SHT35_MEASURE_DELAY_MS;
    }
}

static int sht35_crc_ok(const uint8_t *data, uint8_t crc)
{
    return (driver_crc8(data, 2U, SHT35_CRC_POLY, SHT35_CRC_INIT) == crc) ? DRIVER_OK
                                                                          : (int)HAL_CRC_ERROR;
}

int sht35_soft_reset(sht35_handle_t *handle)
{
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = sht35_write_cmd(handle, SHT35_CMD_SOFT_RESET);
    driver_delay_ms(SHT35_RESET_DELAY_MS);
    return ret;
}

int sht35_set_repeatability(sht35_handle_t *handle, sht35_repeatability_t repeatability)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (repeatability != SHT35_REPEAT_HIGH &&
        repeatability != SHT35_REPEAT_MEDIUM &&
        repeatability != SHT35_REPEAT_LOW) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->repeatability = repeatability;
    return DRIVER_OK;
}

int sht35_set_heater(sht35_handle_t *handle, bool enable)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return sht35_write_cmd(handle,
                           enable ? SHT35_CMD_HEATER_ENABLE : SHT35_CMD_HEATER_DISABLE);
}

int sht35_read_status(sht35_handle_t *handle, uint16_t *status)
{
    uint8_t data[3];
    int ret;

    if (handle == NULL || status == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = sht35_write_cmd(handle, SHT35_CMD_READ_STATUS);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(1U);

    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (handle->crc_enabled && sht35_crc_ok(&data[0], data[2]) != DRIVER_OK) {
        return (int)HAL_CRC_ERROR;
    }

    *status = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    return DRIVER_OK;
}

int sht35_init(sht35_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint16_t status = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SHT35_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->repeatability = SHT35_REPEAT_HIGH;
    handle->crc_enabled = true;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("sht35", "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    (void)sht35_soft_reset(handle);
    handle->initialized = true;

    /* 复位后状态寄存器的 RESET_DETECTED 位置 1，证明器件确实完成复位 */
    if (sht35_read_status(handle, &status) != DRIVER_OK) {
        LOG_W("sht35", "read status failed");
    } else if ((status & SHT35_STATUS_RESET_DETECTED) == 0U) {
        LOG_W("sht35", "unexpected status 0x%04X", status);
    }

    LOG_I("sht35", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int sht35_deinit(sht35_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int sht35_read_raw(sht35_handle_t *handle, uint16_t *raw_temperature, uint16_t *raw_humidity)
{
    uint8_t data[6];
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (raw_temperature == NULL && raw_humidity == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = sht35_write_cmd(handle, sht35_measure_command(handle));
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 命令式器件：必须等待转换完成 */
    driver_delay_ms(sht35_measure_delay(handle));

    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (handle->crc_enabled) {
        if (sht35_crc_ok(&data[0], data[2]) != DRIVER_OK ||
            sht35_crc_ok(&data[3], data[5]) != DRIVER_OK) {
            LOG_E("sht35", "crc error");
            return (int)HAL_CRC_ERROR;
        }
    }

    if (raw_temperature != NULL) {
        *raw_temperature = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    }
    if (raw_humidity != NULL) {
        *raw_humidity = (uint16_t)(((uint16_t)data[3] << 8) | (uint16_t)data[4]);
    }
    return DRIVER_OK;
}

int sht35_read(sht35_handle_t *handle, float *temperature, float *humidity)
{
    uint16_t raw_t = 0U;
    uint16_t raw_h = 0U;
    float value;
    int ret;

    if (temperature == NULL && humidity == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = sht35_read_raw(handle,
                         (temperature != NULL) ? &raw_t : NULL,
                         (humidity != NULL) ? &raw_h : NULL);
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (temperature != NULL) {
        *temperature = SHT35_TEMP_OFFSET +
                        SHT35_TEMP_SCALE * (float)raw_t / SHT35_RAW_MAX;
    }
    if (humidity != NULL) {
        value = SHT35_HUMIDITY_SCALE * (float)raw_h / SHT35_RAW_MAX;
        if (value > 100.0f) {
            value = 100.0f;
        }
        *humidity = value;
    }

    return DRIVER_OK;
}
