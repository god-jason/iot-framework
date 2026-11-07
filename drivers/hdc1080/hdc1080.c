#include "hdc1080.h"
#include "hdc1080_register.h"
#include "log.h"

/* 写配置寄存器（16 位，大端） */
static int hdc1080_write_config(hdc1080_handle_t *handle, uint16_t value)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(value >> 8);
    buf[1] = (uint8_t)(value & 0xFFU);
    return driver_i2c_write_reg(&handle->i2c, HDC1080_REG_CONFIGURATION, buf, sizeof(buf));
}

/* 将寄存器指针指向某个寄存器（单字节写） */
static int hdc1080_set_pointer(hdc1080_handle_t *handle, uint8_t reg)
{
    return driver_i2c_write(&handle->i2c, &reg, 1U);
}

/* 触发一次转换并读回 16 位结果 */
static int hdc1080_convert(hdc1080_handle_t *handle, uint8_t reg, uint16_t *raw)
{
    uint8_t data[2];
    int ret;

    ret = hdc1080_set_pointer(handle, reg);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 指针写入即触发转换，必须等待转换完成后再读 */
    driver_delay_ms(HDC1080_CONVERT_DELAY_MS);

    ret = driver_i2c_read(&handle->i2c, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }

    *raw = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    return DRIVER_OK;
}

int hdc1080_read_id(hdc1080_handle_t *handle, uint16_t *id)
{
    uint8_t data[2];
    int ret;

    if (handle == NULL || id == NULL || handle->i2c.handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = driver_i2c_read_reg(&handle->i2c, HDC1080_REG_SERIAL_HIGH, data, sizeof(data));
    if (ret != DRIVER_OK) {
        return ret;
    }
    *id = (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
    return DRIVER_OK;
}

int hdc1080_set_heater(hdc1080_handle_t *handle, bool enable)
{
    uint16_t config;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    config = (enable ? HDC1080_CONFIG_HEAT : 0U) | HDC1080_CONFIG_14BIT_BOTH;
    ret = hdc1080_write_config(handle, config);
    if (ret == DRIVER_OK) {
        handle->heater_enabled = enable;
    }
    return ret;
}

int hdc1080_init(hdc1080_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint16_t id = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : HDC1080_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->heater_enabled = false;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("hdc1080", "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    /* 软复位（自清零位），复位后需等待 15ms 才能访问 */
    (void)hdc1080_write_config(handle, HDC1080_CONFIG_RST);
    driver_delay_ms(HDC1080_RESET_DELAY_MS);

    handle->initialized = true;

    ret = hdc1080_read_id(handle, &id);
    if (ret != DRIVER_OK) {
        handle->initialized = false;
        LOG_E("hdc1080", "read id failed");
        return (int)IOT_IO_ERROR;
    }
    if (id != HDC1080_DEVICE_ID) {
        handle->initialized = false;
        LOG_E("hdc1080", "unexpected id 0x%04X", id);
        return (int)IOT_NOT_FOUND;
    }

    /* 14 位温度 + 14 位湿度，独立采集模式，加热器关闭 */
    ret = hdc1080_write_config(handle, HDC1080_CONFIG_14BIT_BOTH);
    if (ret != DRIVER_OK) {
        handle->initialized = false;
        return ret;
    }

    LOG_I("hdc1080", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int hdc1080_deinit(hdc1080_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    (void)hdc1080_write_config(handle, HDC1080_CONFIG_14BIT_BOTH);
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int hdc1080_read(hdc1080_handle_t *handle, float *temperature, float *humidity)
{
    uint16_t raw = 0U;
    float value;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (temperature == NULL && humidity == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    /* 先触发温度，再触发湿度（HDC1080 建议顺序） */
    if (temperature != NULL) {
        ret = hdc1080_convert(handle, HDC1080_REG_TEMPERATURE, &raw);
        if (ret != DRIVER_OK) {
            return ret;
        }
        *temperature = HDC1080_TEMP_OFFSET +
                        HDC1080_TEMP_SCALE * (float)raw / HDC1080_RAW_MAX;
    }

    if (humidity != NULL) {
        ret = hdc1080_convert(handle, HDC1080_REG_HUMIDITY, &raw);
        if (ret != DRIVER_OK) {
            return ret;
        }
        value = HDC1080_HUMIDITY_SCALE * (float)raw / HDC1080_RAW_MAX;
        if (value > 100.0f) {
            value = 100.0f;
        }
        *humidity = value;
    }

    return DRIVER_OK;
}
