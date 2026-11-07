#include "asf01.h"
#include "log.h"

/*
 * 通用读取实现：从 0x00 寄存器读 2 字节大端原始值。
 * 若器件使用命令式接口或不同寄存器地址，请在此处按手册调整。
 */

int asf01_init(asf01_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : ASF01_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->raw = 0U;
    handle->concentration = 0.0f;
    handle->scale = 1.0f;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("asf01", "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    handle->initialized = true;
    LOG_I("asf01", "init ok at 0x%02X (generic profile)", handle->i2c.address);
    return DRIVER_OK;
}

int asf01_deinit(asf01_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int asf01_read_raw(asf01_handle_t *handle, uint16_t *raw)
{
    uint8_t d[2];

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (driver_i2c_read(&handle->i2c, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    handle->raw = (uint16_t)(((uint16_t)d[0] << 8) | d[1]);
    *raw = handle->raw;
    return DRIVER_OK;
}

int asf01_read(asf01_handle_t *handle, float *concentration)
{
    uint16_t raw = 0U;
    int ret;

    if (concentration == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = asf01_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    handle->concentration = (float)raw * handle->scale;
    *concentration = handle->concentration;
    return DRIVER_OK;
}

int asf01_set_scale(asf01_handle_t *handle, float scale)
{
    if (handle == NULL || scale <= 0.0f) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->scale = scale;
    return DRIVER_OK;
}
