#include "as5600.h"
#include "as5600_register.h"
#include "log.h"

static int as5600_read_reg(as5600_handle_t *handle, uint8_t reg, uint8_t *buf, size_t len)
{
    return (driver_i2c_read_reg(&handle->i2c, reg, buf, len) == DRIVER_OK) ? DRIVER_OK
                                                                          : (int)PORT_IO_ERROR;
}

static int as5600_write_reg(as5600_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len)
{
    return (driver_i2c_write_reg(&handle->i2c, reg, data, len) == DRIVER_OK) ? DRIVER_OK
                                                                            : (int)PORT_IO_ERROR;
}

static int as5600_read_conf(as5600_handle_t *handle, uint16_t *conf)
{
    uint8_t d[2];
    if (conf == NULL) { return (int)PORT_INVALID_PARAM; }
    if (as5600_read_reg(handle, AS5600_REG_CONF_H, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    *conf = (uint16_t)(((uint16_t)d[0] << 8) | (uint16_t)d[1]);
    return DRIVER_OK;
}

/* 读-改-写 16 位 CONF 寄存器 */
static int as5600_update_conf(as5600_handle_t *handle, uint16_t mask, uint16_t value)
{
    uint8_t d[2];
    uint16_t conf;
    int ret = as5600_read_conf(handle, &conf);
    if (ret != DRIVER_OK) { return ret; }
    conf = (uint16_t)((conf & (uint16_t)~mask) | (value & mask));
    d[0] = (uint8_t)(conf >> 8);
    d[1] = (uint8_t)(conf & 0xFFU);
    if (as5600_write_reg(handle, AS5600_REG_CONF_H, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    handle->conf = conf;
    return DRIVER_OK;
}

int as5600_init(as5600_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint8_t status = 0U;
    uint8_t d[2];
    uint16_t conf = 0U;
    uint16_t verify = 0U;
    if (handle == NULL || i2c == NULL) { return (int)PORT_INVALID_PARAM; }
    handle->i2c = (driver_i2c_t){ .handle = i2c, .timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT,
                                  .address = (address != 0U) ? address : AS5600_DEFAULT_ADDRESS };
    handle->power_mode = AS5600_POWER_MODE_NOMINAL;
    handle->conf = 0U;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("as5600", "no ack at 0x%02X", (int)handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }
    driver_delay_ms(AS5600_BOOT_DELAY_MS);

    /* 无 WHO_AM_I：先保存 CONF，写入已知慢滤波设置后回读校验写通路 */
    if (as5600_read_conf(handle, &conf) != DRIVER_OK) { return (int)PORT_IO_ERROR; }
    d[0] = (uint8_t)(((conf & (uint16_t)~AS5600_CONF_SF_MASK) | AS5600_CONF_VERIFY_VALUE) >> 8);
    d[1] = (uint8_t)(conf & 0xFFU);
    if (as5600_write_reg(handle, AS5600_REG_CONF_H, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (as5600_read_conf(handle, &verify) != DRIVER_OK ||
        (verify & AS5600_CONF_SF_MASK) != AS5600_CONF_VERIFY_VALUE) {
        LOG_E("as5600", "conf readback 0x%04X mismatch", verify);
        return (int)PORT_NOT_FOUND;
    }
    /* 恢复原始配置 */
    if (as5600_update_conf(handle, 0xFFFFU, conf) != DRIVER_OK) { return (int)PORT_IO_ERROR; }
    handle->conf = conf;
    handle->power_mode = (as5600_power_mode_t)(conf & AS5600_CONF_PM_MASK);

    if (as5600_read_reg(handle, AS5600_REG_STATUS, &status, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if ((status & AS5600_STATUS_MD) == 0U) {
        LOG_W("as5600", "no magnet detected, status=0x%02X", status);
    }
    if ((status & (AS5600_STATUS_MH | AS5600_STATUS_ML)) == (AS5600_STATUS_MH | AS5600_STATUS_ML)) {
        LOG_W("as5600", "magnet field out of AGC range, status=0x%02X", status);
    }

    handle->initialized = true;
    LOG_I("as5600", "init ok at 0x%02X, conf=0x%04X", (int)handle->i2c.address, conf);
    return DRIVER_OK;
}

int as5600_deinit(as5600_handle_t *handle)
{
    if (handle == NULL) { return (int)PORT_INVALID_PARAM; }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int as5600_get_status(as5600_handle_t *handle, uint8_t *status)
{
    if (handle == NULL || status == NULL || !handle->initialized) { return (int)PORT_INVALID_PARAM; }
    return as5600_read_reg(handle, AS5600_REG_STATUS, status, 1U);
}

int as5600_read_raw(as5600_handle_t *handle, uint16_t *raw_angle)
{
    uint8_t d[2];
    if (handle == NULL || raw_angle == NULL || !handle->initialized) { return (int)PORT_INVALID_PARAM; }
    if (as5600_read_reg(handle, AS5600_REG_RAW_ANGLE_H, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    *raw_angle = (uint16_t)((((uint16_t)d[0] << 8) | (uint16_t)d[1]) & 0x0FFFU);
    return DRIVER_OK;
}

int as5600_read(as5600_handle_t *handle, as5600_data_t *data)
{
    uint8_t d[AS5600_ANGLE_BURST_LEN];
    uint8_t status;
    if (handle == NULL || data == NULL || !handle->initialized) { return (int)PORT_INVALID_PARAM; }

    /* 0x0B~0x0F：STATUS, RAW_ANGLE(2), ANGLE(2) 连续寄存器 */
    if (as5600_read_reg(handle, AS5600_REG_STATUS, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    status = d[0];
    data->raw_angle = (uint16_t)((((uint16_t)d[1] << 8) | (uint16_t)d[2]) & 0x0FFFU);
    data->angle = (uint16_t)((((uint16_t)d[3] << 8) | (uint16_t)d[4]) & 0x0FFFU);
    data->angle_deg = (float)data->angle * AS5600_DEG_PER_LSB;
    data->magnet_detected = ((status & AS5600_STATUS_MD) != 0U);
    data->magnet_too_strong = ((status & AS5600_STATUS_MH) != 0U);
    data->magnet_too_weak = ((status & AS5600_STATUS_ML) != 0U);
    return DRIVER_OK;
}

int as5600_set_power_mode(as5600_handle_t *handle, as5600_power_mode_t mode)
{
    int ret;
    if (handle == NULL || !handle->initialized ||
        (unsigned)mode > (unsigned)AS5600_POWER_MODE_LPM3) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = as5600_update_conf(handle, AS5600_CONF_PM_MASK,
                             (uint16_t)((uint16_t)mode << AS5600_CONF_PM_SHIFT));
    if (ret == DRIVER_OK) { handle->power_mode = mode; }
    return ret;
}

int as5600_set_hysteresis(as5600_handle_t *handle, uint8_t lsb)
{
    if (handle == NULL || !handle->initialized || lsb > 3U) { return (int)PORT_INVALID_PARAM; }
    return as5600_update_conf(handle, AS5600_CONF_HYST_MASK,
                              (uint16_t)((uint16_t)lsb << AS5600_CONF_HYST_SHIFT));
}

int as5600_set_slow_filter(as5600_handle_t *handle, uint8_t filter)
{
    if (handle == NULL || !handle->initialized || filter > 3U) { return (int)PORT_INVALID_PARAM; }
    return as5600_update_conf(handle, AS5600_CONF_SF_MASK,
                              (uint16_t)((uint16_t)filter << AS5600_CONF_SF_SHIFT));
}
