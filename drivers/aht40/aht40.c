#include "aht40.h"
#include "aht40_register.h"
#include "log.h"

int aht40_get_status(aht40_handle_t *handle, uint8_t *status)
{
    if (handle == NULL || status == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, AHT40_CMD_STATUS, status, 1U);
}

static int aht40_wait_ready(aht40_handle_t *handle)
{
    uint32_t start = driver_tick_ms();
    uint8_t status = 0U;

    for (;;) {
        if (driver_i2c_read_reg(&handle->i2c, AHT40_CMD_STATUS, &status, 1U) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        if ((status & AHT40_STATUS_BUSY) == 0U) {
            return DRIVER_OK;
        }
        if ((uint32_t)(driver_tick_ms() - start) > AHT40_MEASURE_DELAY_MS * 3U) {
            return (int)HAL_TIMEOUT;
        }
        driver_delay_ms(1U);
    }
}

int aht40_init(aht40_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t status = 0U;
    uint8_t calibrate[2] = { 0x08U, 0x00U };
    uint8_t reset = AHT40_CMD_SOFT_RESET;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : AHT40_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    driver_delay_ms(AHT40_POWER_ON_DELAY_MS);

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("aht40", "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    ret = driver_i2c_write(&handle->i2c, &reset, 1U);
    if (ret != DRIVER_OK) {
        LOG_W("aht40", "soft reset failed: %d", ret);
    }
    driver_delay_ms(AHT40_RESET_DELAY_MS);

    if (aht40_wait_ready(handle) != DRIVER_OK) {
        LOG_E("aht40", "device not ready");
        return (int)HAL_NOT_READY;
    }

    if (driver_i2c_read_reg(&handle->i2c, AHT40_CMD_STATUS, &status, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if ((status & AHT40_STATUS_CALIBRATED) == 0U) {
        uint8_t cmd = AHT40_CMD_CALIBRATE;

        if (driver_i2c_write(&handle->i2c, &cmd, 1U) != DRIVER_OK ||
            driver_i2c_write(&handle->i2c, calibrate, sizeof(calibrate)) != DRIVER_OK) {
            LOG_E("aht40", "calibrate failed");
            return (int)HAL_IO_ERROR;
        }
        driver_delay_ms(AHT40_CALIBRATE_DELAY_MS);
    }

    handle->initialized = true;
    LOG_I("aht40", "init ok, status=0x%02X", status);
    return DRIVER_OK;
}

int aht40_deinit(aht40_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int aht40_read(aht40_handle_t *handle, float *temperature, float *humidity)
{
    uint8_t cmd[3] = { AHT40_CMD_TRIGGER, 0x33U, 0x00U };
    uint8_t data[7];
    uint32_t raw_h;
    uint32_t raw_t;
    float value;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (temperature == NULL && humidity == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = driver_i2c_write(&handle->i2c, cmd, sizeof(cmd));
    if (ret != DRIVER_OK) {
        return ret;
    }

    driver_delay_ms(AHT40_MEASURE_DELAY_MS);

    if (aht40_wait_ready(handle) != DRIVER_OK) {
        return (int)HAL_TIMEOUT;
    }

    driver_delay_ms(AHT40_READ_DELAY_MS);
    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    if ((data[0] & AHT40_STATUS_BUSY) != 0U) {
        return (int)HAL_BUSY;
    }

    raw_h = ((uint32_t)data[1] << 12) |
            ((uint32_t)data[2] << 4) |
            ((uint32_t)data[3] >> 4);
    raw_t = ((uint32_t)(data[3] & 0x0FU) << 16) |
            ((uint32_t)data[4] << 8) |
            (uint32_t)data[5];

    if (humidity != NULL) {
        value = (float)raw_h * AHT40_HUMIDITY_SCALE / AHT40_DATA_SCALE;
        if (value > 100.0f) {
            value = 100.0f;
        }
        *humidity = value;
    }
    if (temperature != NULL) {
        /* 20 位无符号原始值，非补码，无需符号扩展 */
        *temperature = (float)raw_t * AHT40_TEMP_SCALE / AHT40_DATA_SCALE +
                        AHT40_TEMP_OFFSET;
    }

    return DRIVER_OK;
}
