#include "aht20.h"
#include "aht20_register.h"
#include "log.h"

static int aht20_wait_ready(aht20_handle_t *handle)
{
    uint32_t start = hal_tick_get();
    uint8_t status = 0U;

    for (;;) {
        if (hal_i2c_dev_read_reg(&handle->i2c, AHT20_CMD_STATUS, &status, 1U) != HAL_OK) {
            return (int)HAL_IO_ERROR;
        }
        if ((status & AHT20_STATUS_BUSY) == 0U) {
            return HAL_OK;
        }
        if ((uint32_t)(hal_tick_get() - start) > AHT20_MEASURE_DELAY_MS * 3U) {
            return (int)HAL_TIMEOUT;
        }
        hal_delay_ms(1U);
    }
}

int aht20_init(aht20_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t status = 0U;
    uint8_t calibrate[2] = { 0x08U, 0x00U };
    uint8_t reset = AHT20_CMD_SOFT_RESET;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : AHT20_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = HAL_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    hal_delay_ms(AHT20_POWER_ON_DELAY_MS);

    /* 软复位，部分模组不支持，失败不视为致命 */
    ret = hal_i2c_dev_write(&handle->i2c, &reset, 1U);
    if (ret != HAL_OK) {
        LOG_W("aht20", "soft reset failed: %d", ret);
    }
    hal_delay_ms(AHT20_RESET_DELAY_MS);

    if (aht20_wait_ready(handle) != HAL_OK) {
        LOG_E("aht20", "device not ready");
        return (int)HAL_NOT_READY;
    }

    /* 读取状态并检查是否已校准 */
    if (hal_i2c_dev_read_reg(&handle->i2c, AHT20_CMD_STATUS, &status, 1U) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    if ((status & AHT20_STATUS_CALIBRATED) == 0U) {
        uint8_t cmd = AHT20_CMD_CALIBRATE;

        if (hal_i2c_dev_write(&handle->i2c, &cmd, 1U) != HAL_OK ||
            hal_i2c_dev_write(&handle->i2c, calibrate, sizeof(calibrate)) != HAL_OK) {
            LOG_E("aht20", "calibrate failed");
            return (int)HAL_IO_ERROR;
        }
        hal_delay_ms(AHT20_CALIBRATE_DELAY_MS);
    }

    handle->initialized = true;
    LOG_I("aht20", "init ok, status=0x%02X", status);
    return HAL_OK;
}

int aht20_deinit(aht20_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return HAL_OK;
}

int aht20_get_status(aht20_handle_t *handle, uint8_t *status)
{
    if (handle == NULL || status == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return hal_i2c_dev_read_reg(&handle->i2c, AHT20_CMD_STATUS, status, 1U);
}

int aht20_read(aht20_handle_t *handle, float *temperature, float *humidity)
{
    uint8_t cmd[3] = { AHT20_CMD_TRIGGER, 0x33U, 0x00U };
    uint8_t data[7];
    uint32_t raw_humidity;
    uint32_t raw_temperature;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (temperature == NULL && humidity == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    if (hal_i2c_dev_write(&handle->i2c, cmd, sizeof(cmd)) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    hal_delay_ms(AHT20_MEASURE_DELAY_MS);

    if (aht20_wait_ready(handle) != HAL_OK) {
        return (int)HAL_TIMEOUT;
    }

    if (hal_i2c_dev_read(&handle->i2c, data, sizeof(data)) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* data[0] 为状态，data[1..6] 为湿度/温度原始值 */
    if ((data[0] & AHT20_STATUS_BUSY) != 0U) {
        return (int)HAL_BUSY;
    }

    raw_humidity = ((uint32_t)data[1] << 12) |
                   ((uint32_t)data[2] << 4) |
                   ((uint32_t)data[3] >> 4);
    raw_temperature = ((uint32_t)(data[3] & 0x0FU) << 16) |
                      ((uint32_t)data[4] << 8) |
                      (uint32_t)data[5];

    if (humidity != NULL) {
        *humidity = (float)raw_humidity * 100.0f / AHT20_DATA_SCALE;
    }
    if (temperature != NULL) {
        *temperature = (float)raw_temperature * 200.0f / AHT20_DATA_SCALE - 50.0f;
    }
    return HAL_OK;
}
