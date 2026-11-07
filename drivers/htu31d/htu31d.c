#include "htu31d.h"
#include "htu31d_register.h"
#include "log.h"

static int htu31d_read_frame(htu31d_handle_t *handle, uint16_t *raw)
{
    uint8_t data[3];
    int ret;

    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (handle->crc_enabled &&
        driver_crc8(data, 2U, HTU31D_CRC_POLY, HTU31D_CRC_INIT) != data[2]) {
        LOG_E("htu31d", "crc error");
        return (int)PORT_CRC_ERROR;
    }

    *raw = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    return DRIVER_OK;
}

static int htu31d_convert(htu31d_handle_t *handle, uint8_t command, uint16_t *raw)
{
    int ret;

    ret = driver_i2c_write(&handle->i2c, &command, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    /* 命令式器件：转换期间器件不响应，必须等待 */
    driver_delay_ms(HTU31D_CONVERT_DELAY_MS);
    return htu31d_read_frame(handle, raw);
}

int htu31d_read_manufacturer_id(htu31d_handle_t *handle, uint16_t *id)
{
    uint8_t command = HTU31D_CMD_READ_MANUFACTURER_ID;
    uint8_t data[3];
    int ret;

    if (handle == NULL || id == NULL || handle->i2c.handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = driver_i2c_write(&handle->i2c, &command, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (handle->crc_enabled &&
        driver_crc8(data, 2U, HTU31D_CRC_POLY, HTU31D_CRC_INIT) != data[2]) {
        LOG_E("htu31d", "id crc error");
        return (int)PORT_CRC_ERROR;
    }

    *id = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    return DRIVER_OK;
}

int htu31d_init(htu31d_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint16_t id = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : HTU31D_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->crc_enabled = true;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("htu31d", "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    handle->initialized = true;

    /*
     * 校验厂商 ID，确认这是 HTU31D 而不是同地址的其它器件。
     * 部分固件不支持该命令（返回 NACK），此时只告警；只要能读到 ID 就必须匹配。
     */
    ret = htu31d_read_manufacturer_id(handle, &id);
    if (ret == DRIVER_OK) {
        if (id != HTU31D_MANUFACTURER_ID) {
            handle->initialized = false;
            LOG_E("htu31d", "unexpected manufacturer id 0x%04X", id);
            return (int)PORT_NOT_FOUND;
        }
    } else {
        LOG_W("htu31d", "manufacturer id not readable: %d", ret);
    }

    LOG_I("htu31d", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int htu31d_deinit(htu31d_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int htu31d_read(htu31d_handle_t *handle, float *temperature, float *humidity)
{
    uint16_t raw = 0U;
    float value;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (temperature == NULL && humidity == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    if (temperature != NULL) {
        ret = htu31d_convert(handle, HTU31D_CMD_CONVERT_TEMP, &raw);
        if (ret != DRIVER_OK) {
            return ret;
        }
        *temperature = HTU31D_TEMP_OFFSET +
                        HTU31D_TEMP_SCALE * (float)raw / HTU31D_RAW_MAX;
    }

    if (humidity != NULL) {
        ret = htu31d_convert(handle, HTU31D_CMD_CONVERT_HUMIDITY, &raw);
        if (ret != DRIVER_OK) {
            return ret;
        }
        value = HTU31D_HUMIDITY_SCALE * (float)raw / HTU31D_RAW_MAX;
        if (value > 100.0f) {
            value = 100.0f;
        }
        *humidity = value;
    }

    return DRIVER_OK;
}
