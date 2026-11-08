#include "htu21d.h"
#include "htu21d_register.h"
#include "log.h"

static int htu21d_send_cmd(htu21d_handle_t *handle, uint8_t command)
{
    return driver_i2c_write(&handle->i2c, &command, 1U);
}

/* 读回一帧测量结果：2 字节数据 + CRC8，并屏蔽状态位 */
static int htu21d_read_frame(htu21d_handle_t *handle, uint16_t *raw)
{
    uint8_t data[3];
    uint16_t value;
    int ret;

    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (handle->crc_enabled &&
        driver_crc8(data, 2U, HTU21D_CRC_POLY, HTU21D_CRC_INIT) != data[2]) {
        LOG_E("htu21d", "crc error");
        return (int)HAL_CRC_ERROR;
    }

    value = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    if ((value & HTU21D_STATUS_VDDS_LOW) != 0U) {
        LOG_W("htu21d", "vdd below 2.25V");
    }
    *raw = (uint16_t)(value & HTU21D_STATUS_MASK);
    return DRIVER_OK;
}

static int htu21d_measure(htu21d_handle_t *handle, uint8_t command,
                          uint32_t delay_ms, uint16_t *raw)
{
    int ret;

    ret = htu21d_send_cmd(handle, command);
    if (ret != DRIVER_OK) {
        return ret;
    }
    /* 命令式器件：必须等待转换完成后再读，否则读到旧数据 */
    driver_delay_ms(delay_ms);
    return htu21d_read_frame(handle, raw);
}

int htu21d_init(htu21d_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t user = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : HTU21D_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->crc_enabled = true;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("htu21d", "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    if (htu21d_soft_reset(handle) != DRIVER_OK) {
        LOG_W("htu21d", "soft reset failed");
    }

    handle->initialized = true;

    /* 用户寄存器复位后默认 0x02（bit1 恒为 1），用于确认器件应答正常 */
    if (htu21d_read_user_register(handle, &user) != DRIVER_OK) {
        handle->initialized = false;
        LOG_E("htu21d", "read user register failed");
        return (int)HAL_IO_ERROR;
    }
    if (user != HTU21D_USER_REG_DEFAULT) {
        LOG_W("htu21d", "unexpected user reg 0x%02X", user);
    }

    LOG_I("htu21d", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int htu21d_deinit(htu21d_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int htu21d_soft_reset(htu21d_handle_t *handle)
{
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = htu21d_send_cmd(handle, HTU21D_CMD_SOFT_RESET);
    driver_delay_ms(HTU21D_RESET_DELAY_MS);
    return ret;
}

int htu21d_read_user_register(htu21d_handle_t *handle, uint8_t *value)
{
    uint8_t command = HTU21D_CMD_READ_USER;
    int ret;

    if (handle == NULL || value == NULL || handle->i2c.handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = driver_i2c_write(&handle->i2c, &command, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    return driver_i2c_read(&handle->i2c, value, 1U);
}

int htu21d_read(htu21d_handle_t *handle, float *temperature, float *humidity)
{
    uint16_t raw_t = 0U;
    uint16_t raw_h = 0U;
    float t;
    float rh;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (temperature == NULL && humidity == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    if (temperature != NULL) {
        ret = htu21d_measure(handle, HTU21D_CMD_MEASURE_T,
                             HTU21D_TEMP_DELAY_MS, &raw_t);
        if (ret != DRIVER_OK) {
            return ret;
        }
        t = HTU21D_TEMP_OFFSET + HTU21D_TEMP_SCALE * (float)raw_t / HTU21D_RAW_MAX;
        *temperature = t;
    }

    if (humidity != NULL) {
        ret = htu21d_measure(handle, HTU21D_CMD_MEASURE_RH,
                             HTU21D_HUMIDITY_DELAY_MS, &raw_h);
        if (ret != DRIVER_OK) {
            return ret;
        }
        rh = HTU21D_HUMIDITY_OFFSET + HTU21D_HUMIDITY_SCALE * (float)raw_h / HTU21D_RAW_MAX;
        if (rh < 0.0f) {
            rh = 0.0f;
        } else if (rh > 100.0f) {
            rh = 100.0f;
        }
        *humidity = rh;
    }

    return DRIVER_OK;
}
