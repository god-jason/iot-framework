#include "sht3x.h"
#include "sht3x_register.h"
#include "log.h"

/*
 * SHT3x 与 SHT30 命令集完全一致（单次测量 0x2C06、软复位 0x30A2、加热器
 * 0x306D/0x3066，CRC8 poly=0x31 init=0xFF）。为避免维护两份 CRC 与转换
 * 公式，这里只做参数校验与转发，实际时序由 drivers/sht30 负责。
 */

static int sht3x_check(const sht3x_handle_t *handle)
{
    if (handle == NULL || !handle->dev.initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int sht3x_init(sht3x_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = sht30_init(&handle->dev, i2c,
                     (address != 0U) ? address : SHT3X_DEFAULT_ADDRESS);
    if (ret != DRIVER_OK) {
        LOG_E("sht3x", "init failed at 0x%02X: %d", (unsigned)address, ret);
        return ret;
    }

    LOG_I("sht3x", "init ok at 0x%02X", (unsigned)handle->dev.i2c.address);
    return DRIVER_OK;
}

int sht3x_deinit(sht3x_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    return sht30_deinit(&handle->dev);
}

int sht3x_soft_reset(sht3x_handle_t *handle)
{
    if (handle == NULL || handle->dev.i2c.handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    return sht30_soft_reset(&handle->dev);
}

int sht3x_set_heater(sht3x_handle_t *handle, bool enable)
{
    if (sht3x_check(handle) != DRIVER_OK) {
        return (int)IOT_INVALID_PARAM;
    }
    return sht30_set_heater(&handle->dev, enable);
}

int sht3x_read_raw(sht3x_handle_t *handle, uint16_t *raw_temperature, uint16_t *raw_humidity)
{
    if (sht3x_check(handle) != DRIVER_OK) {
        return (int)IOT_INVALID_PARAM;
    }
    if (raw_temperature == NULL && raw_humidity == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    return sht30_read_raw(&handle->dev, raw_temperature, raw_humidity);
}

int sht3x_read(sht3x_handle_t *handle, float *temperature, float *humidity)
{
    sht30_data_t data;
    int ret;

    if (sht3x_check(handle) != DRIVER_OK) {
        return (int)IOT_INVALID_PARAM;
    }
    if (temperature == NULL && humidity == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    data.temperature = 0.0f;
    data.humidity = 0.0f;

    ret = sht30_read(&handle->dev, &data);
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (temperature != NULL) {
        *temperature = data.temperature;
    }
    if (humidity != NULL) {
        *humidity = data.humidity;
    }
    return DRIVER_OK;
}
