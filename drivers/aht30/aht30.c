#include "aht30.h"
#include "aht30_register.h"
#include "log.h"

int aht30_get_status(aht30_handle_t *handle, uint8_t *status)
{
    if (handle == NULL || status == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, AHT30_CMD_STATUS, status, 1U);
}

/* 轮询忙标志，超时按测量时间的 3 倍计算 */
static int aht30_wait_ready(aht30_handle_t *handle)
{
    uint32_t start = driver_tick_ms();
    uint8_t status = 0U;

    for (;;) {
        if (driver_i2c_read_reg(&handle->i2c, AHT30_CMD_STATUS, &status, 1U) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
        if ((status & AHT30_STATUS_BUSY) == 0U) {
            return DRIVER_OK;
        }
        if ((uint32_t)(driver_tick_ms() - start) > AHT30_MEASURE_DELAY_MS * 3U) {
            return (int)PORT_TIMEOUT;
        }
        driver_delay_ms(1U);
    }
}

int aht30_init(aht30_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint8_t status = 0U;
    uint8_t calibrate[2] = { 0x08U, 0x00U };
    uint8_t reset = AHT30_CMD_SOFT_RESET;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : AHT30_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    driver_delay_ms(AHT30_POWER_ON_DELAY_MS);

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("aht30", "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    /* 软复位；部分模组不支持该命令，失败不视为致命 */
    ret = driver_i2c_write(&handle->i2c, &reset, 1U);
    if (ret != DRIVER_OK) {
        LOG_W("aht30", "soft reset failed: %d", ret);
    }
    driver_delay_ms(AHT30_RESET_DELAY_MS);

    if (aht30_wait_ready(handle) != DRIVER_OK) {
        LOG_E("aht30", "device not ready");
        return (int)PORT_NOT_READY;
    }

    if (driver_i2c_read_reg(&handle->i2c, AHT30_CMD_STATUS, &status, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if ((status & AHT30_STATUS_CALIBRATED) == 0U) {
        uint8_t cmd = AHT30_CMD_CALIBRATE;

        if (driver_i2c_write(&handle->i2c, &cmd, 1U) != DRIVER_OK ||
            driver_i2c_write(&handle->i2c, calibrate, sizeof(calibrate)) != DRIVER_OK) {
            LOG_E("aht30", "calibrate failed");
            return (int)PORT_IO_ERROR;
        }
        driver_delay_ms(AHT30_CALIBRATE_DELAY_MS);
    }

    handle->initialized = true;
    LOG_I("aht30", "init ok, status=0x%02X", status);
    return DRIVER_OK;
}

int aht30_deinit(aht30_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int aht30_read(aht30_handle_t *handle, float *temperature, float *humidity)
{
    uint8_t cmd[3] = { AHT30_CMD_TRIGGER, 0x33U, 0x00U };
    uint8_t data[7];
    uint32_t raw_h;
    uint32_t raw_t;
    float value;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (temperature == NULL && humidity == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = driver_i2c_write(&handle->i2c, cmd, sizeof(cmd));
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 命令式器件：20 位转换需要约 80ms，必须等待 */
    driver_delay_ms(AHT30_MEASURE_DELAY_MS);

    if (aht30_wait_ready(handle) != DRIVER_OK) {
        return (int)PORT_TIMEOUT;
    }

    driver_delay_ms(AHT30_READ_DELAY_MS);
    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    if ((data[0] & AHT30_STATUS_BUSY) != 0U) {
        return (int)PORT_BUSY;
    }

    /* 20 位无符号原始值：湿度 [1..3] 高 20 位，温度从 data[3] 低 4 位开始 */
    raw_h = ((uint32_t)data[1] << 12) |
            ((uint32_t)data[2] << 4) |
            ((uint32_t)data[3] >> 4);
    raw_t = ((uint32_t)(data[3] & 0x0FU) << 16) |
            ((uint32_t)data[4] << 8) |
            (uint32_t)data[5];

    if (humidity != NULL) {
        value = (float)raw_h * AHT30_HUMIDITY_SCALE / AHT30_DATA_SCALE;
        if (value > 100.0f) {
            value = 100.0f;
        }
        *humidity = value;
    }
    if (temperature != NULL) {
        /* 温度为无符号 20 位（非补码），无需符号扩展，直接减偏移 */
        *temperature = (float)raw_t * AHT30_TEMP_SCALE / AHT30_DATA_SCALE +
                        AHT30_TEMP_OFFSET;
    }

    return DRIVER_OK;
}
