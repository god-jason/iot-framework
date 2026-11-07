#include "sht31.h"
#include "sht31_register.h"
#include "log.h"

static int sht31_write_cmd(sht31_handle_t *handle, uint16_t command)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

static uint16_t sht31_measure_command(sht31_handle_t *handle)
{
    switch (handle->repeatability) {
    case SHT31_REPEAT_MEDIUM:
        return SHT31_CMD_MEASURE_MEDIUM;
    case SHT31_REPEAT_LOW:
        return SHT31_CMD_MEASURE_LOW;
    case SHT31_REPEAT_HIGH:
    default:
        return SHT31_CMD_MEASURE_HIGH;
    }
}

static uint32_t sht31_measure_delay(sht31_handle_t *handle)
{
    switch (handle->repeatability) {
    case SHT31_REPEAT_MEDIUM:
        return SHT31_MEASURE_DELAY_MED_MS;
    case SHT31_REPEAT_LOW:
        return SHT31_MEASURE_DELAY_LOW_MS;
    case SHT31_REPEAT_HIGH:
    default:
        return SHT31_MEASURE_DELAY_MS;
    }
}

static int sht31_crc_ok(const uint8_t *data, uint8_t crc)
{
    return (driver_crc8(data, 2U, SHT31_CRC_POLY, SHT31_CRC_INIT) == crc) ? DRIVER_OK
                                                                          : (int)PORT_CRC_ERROR;
}

int sht31_soft_reset(sht31_handle_t *handle)
{
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = sht31_write_cmd(handle, SHT31_CMD_SOFT_RESET);
    driver_delay_ms(SHT31_RESET_DELAY_MS);
    return ret;
}

int sht31_set_repeatability(sht31_handle_t *handle, sht31_repeatability_t repeatability)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (repeatability != SHT31_REPEAT_HIGH &&
        repeatability != SHT31_REPEAT_MEDIUM &&
        repeatability != SHT31_REPEAT_LOW) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->repeatability = repeatability;
    return DRIVER_OK;
}

int sht31_set_heater(sht31_handle_t *handle, bool enable)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return sht31_write_cmd(handle,
                           enable ? SHT31_CMD_HEATER_ENABLE : SHT31_CMD_HEATER_DISABLE);
}

int sht31_init(sht31_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SHT31_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->repeatability = SHT31_REPEAT_HIGH;
    handle->crc_enabled = true;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("sht31", "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    (void)sht31_soft_reset(handle);
    handle->initialized = true;
    LOG_I("sht31", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int sht31_deinit(sht31_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int sht31_read_raw(sht31_handle_t *handle, uint16_t *raw_temperature, uint16_t *raw_humidity)
{
    uint8_t data[6];
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (raw_temperature == NULL && raw_humidity == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = sht31_write_cmd(handle, sht31_measure_command(handle));
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 命令式器件：转换完成前器件 NACK，必须等待 */
    driver_delay_ms(sht31_measure_delay(handle));

    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (handle->crc_enabled) {
        if (sht31_crc_ok(&data[0], data[2]) != DRIVER_OK ||
            sht31_crc_ok(&data[3], data[5]) != DRIVER_OK) {
            LOG_E("sht31", "crc error");
            return (int)PORT_CRC_ERROR;
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

int sht31_read_status(sht31_handle_t *handle, uint16_t *status)
{
    uint8_t data[3];
    int ret;

    if (handle == NULL || status == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = sht31_write_cmd(handle, SHT31_CMD_READ_STATUS);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(1U);

    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (handle->crc_enabled && sht31_crc_ok(&data[0], data[2]) != DRIVER_OK) {
        return (int)PORT_CRC_ERROR;
    }

    *status = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    return DRIVER_OK;
}

int sht31_read(sht31_handle_t *handle, float *temperature, float *humidity)
{
    uint16_t raw_t = 0U;
    uint16_t raw_h = 0U;
    float value;
    int ret;

    if (temperature == NULL && humidity == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = sht31_read_raw(handle,
                         (temperature != NULL) ? &raw_t : NULL,
                         (humidity != NULL) ? &raw_h : NULL);
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (temperature != NULL) {
        *temperature = SHT31_TEMP_OFFSET +
                        SHT31_TEMP_SCALE * (float)raw_t / SHT31_RAW_MAX;
    }
    if (humidity != NULL) {
        value = SHT31_HUMIDITY_SCALE * (float)raw_h / SHT31_RAW_MAX;
        if (value > 100.0f) {
            value = 100.0f;
        }
        *humidity = value;
    }

    return DRIVER_OK;
}
