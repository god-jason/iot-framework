#include "sht4x.h"
#include "sht4x_register.h"
#include "log.h"

static int sht4x_crc_ok(const uint8_t *data, uint8_t crc)
{
    return (driver_crc8(data, 2U, SHT4X_CRC_POLY, SHT4X_CRC_INIT) == crc) ? DRIVER_OK
                                                                          : (int)PORT_CRC_ERROR;
}

static uint8_t sht4x_measure_command(sht4x_handle_t *handle)
{
    switch (handle->repeatability) {
    case SHT4X_REPEAT_MEDIUM:
        return SHT4X_CMD_MEASURE_MEDIUM;
    case SHT4X_REPEAT_LOW:
        return SHT4X_CMD_MEASURE_LOW;
    case SHT4X_REPEAT_HIGH:
    default:
        return SHT4X_CMD_MEASURE_HIGH;
    }
}

static uint32_t sht4x_measure_delay(sht4x_handle_t *handle)
{
    switch (handle->repeatability) {
    case SHT4X_REPEAT_MEDIUM:
        return SHT4X_MEASURE_DELAY_MED_MS;
    case SHT4X_REPEAT_LOW:
        return SHT4X_MEASURE_DELAY_LOW_MS;
    case SHT4X_REPEAT_HIGH:
    default:
        return SHT4X_MEASURE_DELAY_MS;
    }
}

int sht4x_soft_reset(sht4x_handle_t *handle)
{
    uint8_t command = SHT4X_CMD_SOFT_RESET;
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = driver_i2c_write(&handle->i2c, &command, 1U);
    driver_delay_ms(SHT4X_RESET_DELAY_MS);
    return ret;
}

int sht4x_read_serial(sht4x_handle_t *handle, uint16_t *serial)
{
    uint8_t command = SHT4X_CMD_READ_SERIAL;
    uint8_t data[3];
    int ret;

    if (handle == NULL || serial == NULL || handle->i2c.handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = driver_i2c_write(&handle->i2c, &command, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(SHT4X_MEASURE_DELAY_LOW_MS);

    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (handle->crc_enabled && sht4x_crc_ok(&data[0], data[2]) != DRIVER_OK) {
        LOG_E("sht4x", "serial crc error");
        return (int)PORT_CRC_ERROR;
    }

    *serial = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    return DRIVER_OK;
}

int sht4x_set_repeatability(sht4x_handle_t *handle, sht4x_repeatability_t repeatability)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (repeatability != SHT4X_REPEAT_HIGH &&
        repeatability != SHT4X_REPEAT_MEDIUM &&
        repeatability != SHT4X_REPEAT_LOW) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->repeatability = repeatability;
    return DRIVER_OK;
}

int sht4x_init(sht4x_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint16_t serial = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SHT4X_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->repeatability = SHT4X_REPEAT_HIGH;
    handle->crc_enabled = true;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("sht4x", "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    (void)sht4x_soft_reset(handle);
    handle->initialized = true;

    /* 读序列号并校验 CRC：确认器件为 SHT4x 且总线数据可靠 */
    if (sht4x_read_serial(handle, &serial) != DRIVER_OK) {
        handle->initialized = false;
        LOG_E("sht4x", "read serial failed");
        return (int)PORT_IO_ERROR;
    }

    LOG_I("sht4x", "init ok at 0x%02X, sn=0x%04X", handle->i2c.address, serial);
    return DRIVER_OK;
}

int sht4x_deinit(sht4x_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int sht4x_read(sht4x_handle_t *handle, float *temperature, float *humidity)
{
    uint8_t command;
    uint8_t data[6];
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

    command = sht4x_measure_command(handle);
    ret = driver_i2c_write(&handle->i2c, &command, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 命令式器件：转换期间器件不应答，必须按重复性等待 */
    driver_delay_ms(sht4x_measure_delay(handle));

    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (handle->crc_enabled) {
        if (sht4x_crc_ok(&data[0], data[2]) != DRIVER_OK ||
            sht4x_crc_ok(&data[3], data[5]) != DRIVER_OK) {
            LOG_E("sht4x", "crc error");
            return (int)PORT_CRC_ERROR;
        }
    }

    raw_t = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    raw_h = (uint16_t)(((uint16_t)data[3] << 8) | (uint16_t)data[4]);

    if (temperature != NULL) {
        *temperature = SHT4X_TEMP_OFFSET +
                        SHT4X_TEMP_SCALE * (float)raw_t / SHT4X_RAW_MAX;
    }
    if (humidity != NULL) {
        value = SHT4X_HUMIDITY_OFFSET +
                SHT4X_HUMIDITY_SCALE * (float)raw_h / SHT4X_RAW_MAX;
        if (value < 0.0f) {
            value = 0.0f;
        } else if (value > 100.0f) {
            value = 100.0f;
        }
        *humidity = value;
    }

    return DRIVER_OK;
}
