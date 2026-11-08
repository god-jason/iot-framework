#include "sht30.h"
#include "sht30_register.h"
#include "log.h"

static int sht30_write_cmd(sht30_handle_t *handle, uint16_t command)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

static uint16_t sht30_measure_command(sht30_handle_t *handle)
{
    switch (handle->repeatability) {
    case SHT30_REPEAT_MEDIUM:
        return SHT30_CMD_MEASURE_MEDIUM;
    case SHT30_REPEAT_LOW:
        return SHT30_CMD_MEASURE_LOW;
    case SHT30_REPEAT_HIGH:
    default:
        return SHT30_CMD_MEASURE_HIGH;
    }
}

static int sht30_check_crc(const uint8_t *data, uint8_t crc)
{
    return (driver_crc8(data, 2U, SHT30_CRC_POLY, SHT30_CRC_INIT) == crc) ? DRIVER_OK
                                                                          : (int)HAL_CRC_ERROR;
}

int sht30_init(sht30_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SHT30_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->repeatability = SHT30_REPEAT_HIGH;
    handle->crc_enabled = true;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("sht30", "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    (void)sht30_soft_reset(handle);
    handle->initialized = true;
    LOG_I("sht30", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int sht30_deinit(sht30_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int sht30_soft_reset(sht30_handle_t *handle)
{
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = sht30_write_cmd(handle, SHT30_CMD_SOFT_RESET);
    driver_delay_ms(SHT30_RESET_DELAY_MS);
    return ret;
}

int sht30_set_heater(sht30_handle_t *handle, bool enable)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return sht30_write_cmd(handle,
                           enable ? SHT30_CMD_HEATER_ENABLE : SHT30_CMD_HEATER_DISABLE);
}

int sht30_read_raw(sht30_handle_t *handle, uint16_t *raw_temperature, uint16_t *raw_humidity)
{
    uint8_t data[6];
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = sht30_write_cmd(handle, sht30_measure_command(handle));
    if (ret != DRIVER_OK) {
        return ret;
    }

    driver_delay_ms(SHT30_MEASURE_DELAY_MS);

    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (handle->crc_enabled) {
        if (sht30_check_crc(&data[0], data[2]) != DRIVER_OK ||
            sht30_check_crc(&data[3], data[5]) != DRIVER_OK) {
            LOG_E("sht30", "crc error");
            return (int)HAL_CRC_ERROR;
        }
    }

    if (raw_temperature != NULL) {
        *raw_temperature = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    }
    if (raw_humidity != NULL) {
        *raw_humidity = (uint16_t)(((uint16_t)data[3] << 8) | data[4]);
    }
    return DRIVER_OK;
}

int sht30_read(sht30_handle_t *handle, sht30_data_t *data)
{
    uint16_t raw_t = 0U;
    uint16_t raw_h = 0U;
    int ret;

    if (data == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = sht30_read_raw(handle, &raw_t, &raw_h);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->temperature = SHT30_TEMP_OFFSET +
                        SHT30_TEMP_SCALE * (float)raw_t / SHT30_RAW_MAX;
    data->humidity = SHT30_HUMIDITY_SCALE * (float)raw_h / SHT30_RAW_MAX;
    if (data->humidity > 100.0f) {
        data->humidity = 100.0f;
    }
    return DRIVER_OK;
}
