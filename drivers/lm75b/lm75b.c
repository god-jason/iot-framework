#include "lm75b.h"
#include "lm75b_register.h"
#include "iot_logger.h"

/* 温度码 -> 摄氏度：右侧 5 位为小数（已左对齐），高 11 位为有符号整数 */
static float lm75b_decode(uint16_t raw)
{
    int16_t aligned = (int16_t)raw;

    return (float)(aligned >> LM75B_DATA_SHIFT) * LM75B_LSB_CELSIUS;
}

/* 摄氏度 -> 左对齐 11 位温度码 */
static uint16_t lm75b_encode(float celsius)
{
    float scaled = celsius / LM75B_LSB_CELSIUS;
    int32_t code;

    /* 限幅到 11 位有符号范围（-1024 - 1023 个 LSB） */
    if (scaled > 1023.0f) {
        scaled = 1023.0f;
    }
    if (scaled < -1024.0f) {
        scaled = -1024.0f;
    }
    code = (int32_t)scaled;
    return (uint16_t)((uint16_t)((int16_t)code) << LM75B_DATA_SHIFT);
}

int lm75b_init(lm75b_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? (uint16_t)(address & LM75B_ADDRESS_MASK)
                                          : LM75B_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("lm75b", "probe failed, addr=0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    driver_delay_ms(LM75B_CONV_DELAY_MS);

    handle->initialized = true;
    LOG_I("lm75b", "init ok");
    return DRIVER_OK;
}

int lm75b_deinit(lm75b_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)PORT_NOT_READY;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int lm75b_read_raw(lm75b_handle_t *handle, int16_t *raw)
{
    uint8_t buf[2];

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    if (driver_i2c_read_reg(&handle->i2c, LM75B_REG_TEMPERATURE, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    *raw = (int16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
    return DRIVER_OK;
}

int lm75b_read(lm75b_handle_t *handle, float *celsius)
{
    int16_t raw = 0;
    int ret;

    if (celsius == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = lm75b_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *celsius = lm75b_decode((uint16_t)raw);
    return DRIVER_OK;
}

int lm75b_get_config(lm75b_handle_t *handle, uint8_t *config)
{
    if (handle == NULL || config == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, LM75B_REG_CONFIG, config, 1U);
}

int lm75b_set_config(lm75b_handle_t *handle, uint8_t config)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (driver_i2c_write_reg(&handle->i2c, LM75B_REG_CONFIG, &config, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    /* 从关断模式唤醒后需等待一次转换 */
    driver_delay_ms(LM75B_CONV_DELAY_MS);
    return DRIVER_OK;
}

static int lm75b_write_threshold(lm75b_handle_t *handle, uint8_t reg, float celsius)
{
    uint16_t code;
    uint8_t buf[2];

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    code = lm75b_encode(celsius);
    buf[0] = (uint8_t)(code >> 8);
    buf[1] = (uint8_t)(code & 0xFFU);
    if (driver_i2c_write_reg(&handle->i2c, reg, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    return DRIVER_OK;
}

int lm75b_set_overtemperature(lm75b_handle_t *handle, float celsius)
{
    return lm75b_write_threshold(handle, LM75B_REG_TOS, celsius);
}

int lm75b_set_hysteresis(lm75b_handle_t *handle, float celsius)
{
    return lm75b_write_threshold(handle, LM75B_REG_THYST, celsius);
}
