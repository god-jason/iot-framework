#include "sts4x.h"
#include "log.h"

#define STS4X_TAG "sts4x"

static int sts4x_check(const sts4x_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

static uint8_t sts4x_measure_command(const sts4x_handle_t *handle)
{
    switch (handle->repeatability) {
    case STS4X_REPEAT_MEDIUM:
        return STS4X_CMD_MEASURE_MEDIUM;
    case STS4X_REPEAT_LOW:
        return STS4X_CMD_MEASURE_LOW;
    case STS4X_REPEAT_HIGH:
    default:
        return STS4X_CMD_MEASURE_HIGH;
    }
}

static uint32_t sts4x_measure_delay(const sts4x_handle_t *handle)
{
    switch (handle->repeatability) {
    case STS4X_REPEAT_MEDIUM:
        return STS4X_MEASURE_MEDIUM_MS;
    case STS4X_REPEAT_LOW:
        return STS4X_MEASURE_LOW_MS;
    case STS4X_REPEAT_HIGH:
    default:
        return STS4X_MEASURE_HIGH_MS;
    }
}

int sts4x_init(sts4x_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : STS4X_ADDRESS_DEFAULT;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->repeatability = STS4X_REPEAT_HIGH;
    handle->crc_enabled = true;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E(STS4X_TAG, "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    handle->initialized = true;
    (void)sts4x_soft_reset(handle);
    LOG_I(STS4X_TAG, "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int sts4x_deinit(sts4x_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int sts4x_soft_reset(sts4x_handle_t *handle)
{
    uint8_t cmd = STS4X_CMD_SOFT_RESET;
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = driver_i2c_write(&handle->i2c, &cmd, 1U);
    driver_delay_ms(STS4X_RESET_DELAY_MS);
    return ret;
}

int sts4x_set_repeatability(sts4x_handle_t *handle, sts4x_repeatability_t repeatability)
{
    int ret = sts4x_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (repeatability != STS4X_REPEAT_HIGH && repeatability != STS4X_REPEAT_MEDIUM &&
        repeatability != STS4X_REPEAT_LOW) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->repeatability = repeatability;
    return DRIVER_OK;
}

int sts4x_set_crc(sts4x_handle_t *handle, bool enable)
{
    int ret = sts4x_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    handle->crc_enabled = enable;
    return DRIVER_OK;
}

int sts4x_read_raw(sts4x_handle_t *handle, uint16_t *raw)
{
    uint8_t cmd;
    uint8_t buf[3];
    int ret;

    if (raw == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = sts4x_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    cmd = sts4x_measure_command(handle);
    ret = driver_i2c_write(&handle->i2c, &cmd, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(sts4x_measure_delay(handle));

    ret = driver_i2c_read(&handle->i2c, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (handle->crc_enabled &&
        driver_crc8(buf, 2U, STS4X_CRC_POLY, STS4X_CRC_INIT) != buf[2]) {
        LOG_E(STS4X_TAG, "crc error");
        return (int)PORT_CRC_ERROR;
    }

    *raw = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    return DRIVER_OK;
}

int sts4x_read(sts4x_handle_t *handle, float *celsius)
{
    uint16_t raw = 0U;
    int ret;

    if (celsius == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = sts4x_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *celsius = STS4X_TEMP_OFFSET_C + (STS4X_TEMP_SCALE_C * (float)raw / STS4X_RAW_MAX);
    return DRIVER_OK;
}

int sts4x_read_serial(sts4x_handle_t *handle, uint8_t serial[6])
{
    uint8_t cmd = STS4X_CMD_READ_SERIAL;
    uint8_t buf[9];
    uint8_t i;
    int ret;

    if (serial == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = sts4x_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = driver_i2c_write(&handle->i2c, &cmd, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(1U);

    ret = driver_i2c_read(&handle->i2c, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }

    for (i = 0U; i < 3U; i++) {
        if (handle->crc_enabled &&
            driver_crc8(&buf[i * 3U], 2U, STS4X_CRC_POLY, STS4X_CRC_INIT) != buf[(i * 3U) + 2U]) {
            LOG_E(STS4X_TAG, "serial crc error");
            return (int)PORT_CRC_ERROR;
        }
        serial[i * 2U] = buf[i * 3U];
        serial[(i * 2U) + 1U] = buf[(i * 3U) + 1U];
    }
    return DRIVER_OK;
}
