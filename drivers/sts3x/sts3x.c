#include "sts3x.h"
#include "log.h"

#define STS3X_TAG "sts3x"

static int sts3x_check(const sts3x_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return DRIVER_OK;
}

static int sts3x_write_cmd(sts3x_handle_t *handle, uint16_t command)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(command >> 8);
    buf[1] = (uint8_t)(command & 0xFFU);
    return driver_i2c_write(&handle->i2c, buf, sizeof(buf));
}

static uint16_t sts3x_measure_command(const sts3x_handle_t *handle)
{
    switch (handle->repeatability) {
    case STS3X_REPEAT_MEDIUM:
        return STS3X_CMD_MEASURE_MEDIUM;
    case STS3X_REPEAT_LOW:
        return STS3X_CMD_MEASURE_LOW;
    case STS3X_REPEAT_HIGH:
    default:
        return STS3X_CMD_MEASURE_HIGH;
    }
}

static uint32_t sts3x_measure_delay(const sts3x_handle_t *handle)
{
    switch (handle->repeatability) {
    case STS3X_REPEAT_MEDIUM:
        return STS3X_MEASURE_MEDIUM_MS;
    case STS3X_REPEAT_LOW:
        return STS3X_MEASURE_LOW_MS;
    case STS3X_REPEAT_HIGH:
    default:
        return STS3X_MEASURE_HIGH_MS;
    }
}

int sts3x_init(sts3x_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : STS3X_ADDRESS_DEFAULT;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->repeatability = STS3X_REPEAT_HIGH;
    handle->crc_enabled = true;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E(STS3X_TAG, "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    handle->initialized = true;
    (void)sts3x_soft_reset(handle);
    LOG_I(STS3X_TAG, "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int sts3x_deinit(sts3x_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int sts3x_soft_reset(sts3x_handle_t *handle)
{
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = sts3x_write_cmd(handle, STS3X_CMD_SOFT_RESET);
    driver_delay_ms(STS3X_RESET_DELAY_MS);
    return ret;
}

int sts3x_set_repeatability(sts3x_handle_t *handle, sts3x_repeatability_t repeatability)
{
    int ret = sts3x_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (repeatability != STS3X_REPEAT_HIGH && repeatability != STS3X_REPEAT_MEDIUM &&
        repeatability != STS3X_REPEAT_LOW) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->repeatability = repeatability;
    return DRIVER_OK;
}

int sts3x_set_crc(sts3x_handle_t *handle, bool enable)
{
    int ret = sts3x_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    handle->crc_enabled = enable;
    return DRIVER_OK;
}

int sts3x_read_status(sts3x_handle_t *handle, uint16_t *status)
{
    uint8_t buf[3];
    int ret;

    if (status == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = sts3x_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = sts3x_write_cmd(handle, STS3X_CMD_READ_STATUS);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(1U);
    ret = driver_i2c_read(&handle->i2c, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (handle->crc_enabled) {
        if (driver_crc8(buf, 2U, STS3X_CRC_POLY, STS3X_CRC_INIT) != buf[2]) {
            return (int)HAL_CRC_ERROR;
        }
    }
    *status = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    return DRIVER_OK;
}

int sts3x_read_raw(sts3x_handle_t *handle, uint16_t *raw)
{
    uint8_t buf[3];
    int ret;

    if (raw == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = sts3x_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = sts3x_write_cmd(handle, sts3x_measure_command(handle));
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(sts3x_measure_delay(handle));

    ret = driver_i2c_read(&handle->i2c, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (handle->crc_enabled &&
        driver_crc8(buf, 2U, STS3X_CRC_POLY, STS3X_CRC_INIT) != buf[2]) {
        LOG_E(STS3X_TAG, "crc error");
        return (int)HAL_CRC_ERROR;
    }

    *raw = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    return DRIVER_OK;
}

int sts3x_read(sts3x_handle_t *handle, float *celsius)
{
    uint16_t raw = 0U;
    int ret;

    if (celsius == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = sts3x_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *celsius = STS3X_TEMP_OFFSET_C + (STS3X_TEMP_SCALE_C * (float)raw / STS3X_RAW_MAX);
    return DRIVER_OK;
}
