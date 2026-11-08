#include "dht20.h"
#include "dht20_register.h"
#include "log.h"

int dht20_get_status(dht20_handle_t *handle, uint8_t *status)
{
    if (handle == NULL || status == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, DHT20_CMD_STATUS, status, 1U);
}

static int dht20_wait_ready(dht20_handle_t *handle)
{
    uint32_t start = driver_tick_ms();
    uint8_t status = 0U;

    for (;;) {
        if (driver_i2c_read_reg(&handle->i2c, DHT20_CMD_STATUS, &status, 1U) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        if ((status & DHT20_STATUS_BUSY) == 0U) {
            return DRIVER_OK;
        }
        if ((uint32_t)(driver_tick_ms() - start) > DHT20_MEASURE_DELAY_MS * 3U) {
            return (int)HAL_TIMEOUT;
        }
        driver_delay_ms(1U);
    }
}

/* 触发一次测量并读回 7 字节原始帧 */
static int dht20_read_frame(dht20_handle_t *handle, uint8_t data[7])
{
    uint8_t cmd[3] = { DHT20_CMD_TRIGGER, 0x33U, 0x00U };
    int ret;

    ret = driver_i2c_write(&handle->i2c, cmd, sizeof(cmd));
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 命令式器件：20 位转换约 80ms，必须等待后再读 */
    driver_delay_ms(DHT20_MEASURE_DELAY_MS);

    if (dht20_wait_ready(handle) != DRIVER_OK) {
        return (int)HAL_TIMEOUT;
    }

    driver_delay_ms(DHT20_READ_DELAY_MS);
    ret = driver_i2c_read(&handle->i2c, data, 7U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if ((data[0] & DHT20_STATUS_BUSY) != 0U) {
        return (int)HAL_BUSY;
    }
    return DRIVER_OK;
}

int dht20_init(dht20_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t status = 0U;
    uint8_t calibrate[2] = { 0x08U, 0x00U };
    uint8_t reset = DHT20_CMD_SOFT_RESET;
    uint8_t frame[7];
    uint8_t crc;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : DHT20_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->crc_enabled = false;
    handle->initialized = false;

    driver_delay_ms(DHT20_POWER_ON_DELAY_MS);

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("dht20", "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    ret = driver_i2c_write(&handle->i2c, &reset, 1U);
    if (ret != DRIVER_OK) {
        LOG_W("dht20", "soft reset failed: %d", ret);
    }
    driver_delay_ms(DHT20_RESET_DELAY_MS);

    if (dht20_wait_ready(handle) != DRIVER_OK) {
        LOG_E("dht20", "device not ready");
        return (int)HAL_NOT_READY;
    }

    if (driver_i2c_read_reg(&handle->i2c, DHT20_CMD_STATUS, &status, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if ((status & DHT20_STATUS_CALIBRATED) == 0U) {
        uint8_t cmd = DHT20_CMD_CALIBRATE;

        if (driver_i2c_write(&handle->i2c, &cmd, 1U) != DRIVER_OK ||
            driver_i2c_write(&handle->i2c, calibrate, sizeof(calibrate)) != DRIVER_OK) {
            LOG_E("dht20", "calibrate failed");
            return (int)HAL_IO_ERROR;
        }
        driver_delay_ms(DHT20_CALIBRATE_DELAY_MS);
    }

    handle->initialized = true;

    /* 试读一帧：若 CRC 与计算值一致则后续强制校验，否则关闭 CRC 校验 */
    ret = dht20_read_frame(handle, frame);
    if (ret != DRIVER_OK) {
        handle->initialized = false;
        LOG_E("dht20", "first measurement failed: %d", ret);
        return ret;
    }
    crc = driver_crc8(frame, 6U, DHT20_CRC_POLY, DHT20_CRC_INIT);
    if (crc == frame[6]) {
        handle->crc_enabled = true;
    } else {
        LOG_W("dht20", "frame crc 0x%02X != calc 0x%02X, crc check disabled",
              frame[6], crc);
    }

    LOG_I("dht20", "init ok at 0x%02X, crc=%u", handle->i2c.address,
          (unsigned int)(handle->crc_enabled ? 1U : 0U));
    return DRIVER_OK;
}

int dht20_deinit(dht20_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int dht20_read(dht20_handle_t *handle, float *temperature, float *humidity)
{
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

    ret = dht20_read_frame(handle, data);
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (handle->crc_enabled &&
        driver_crc8(data, 6U, DHT20_CRC_POLY, DHT20_CRC_INIT) != data[6]) {
        LOG_E("dht20", "crc error");
        return (int)HAL_CRC_ERROR;
    }

    /* 20 位无符号原始值：湿度 [1..3] 高 20 位，温度从 data[3] 低 4 位开始 */
    raw_h = ((uint32_t)data[1] << 12) |
            ((uint32_t)data[2] << 4) |
            ((uint32_t)data[3] >> 4);
    raw_t = ((uint32_t)(data[3] & 0x0FU) << 16) |
            ((uint32_t)data[4] << 8) |
            (uint32_t)data[5];

    if (humidity != NULL) {
        value = (float)raw_h * DHT20_HUMIDITY_SCALE / DHT20_DATA_SCALE;
        if (value > 100.0f) {
            value = 100.0f;
        }
        *humidity = value;
    }
    if (temperature != NULL) {
        /* 温度同样是无符号 20 位，不做符号扩展 */
        *temperature = (float)raw_t * DHT20_TEMP_SCALE / DHT20_DATA_SCALE +
                        DHT20_TEMP_OFFSET;
    }

    return DRIVER_OK;
}
