#include "amg8833.h"
#include "amg8833_register.h"
#include "iot_logger.h"

#define AMG8833_TAG "amg8833"

static int amg8833_check(const amg8833_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

static float amg8833_pixel_to_c(int16_t raw)
{
    int16_t value = raw;

    /* bit11 为符号位，做 12 位符号扩展 */
    if ((value & AMG8833_PIXEL_SIGN) != 0) {
        value = (int16_t)(value | (int16_t)0xF000);
    }
    return (float)value * AMG8833_PIXEL_LSB_C;
}

static int16_t amg8833_therm_raw(int16_t raw)
{
    int16_t value = raw;

    if ((value & AMG8833_PIXEL_SIGN) != 0) {
        value = (int16_t)(value | (int16_t)0xF000);
    }
    return value;
}

int amg8833_init(amg8833_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint8_t value;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : AMG8833_ADDRESS_DEFAULT;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->frame_rate = AMG8833_FRAME_RATE_10FPS;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E(AMG8833_TAG, "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    /* 进入 normal 模式 */
    value = AMG8833_PCLT_NORMAL;
    if (amg8833_write(handle, AMG8833_REG_PCLT, &value, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    /* 清除复位标志 */
    value = AMG8833_RST_INITIAL;
    if (amg8833_write(handle, AMG8833_REG_RST, &value, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(AMG8833_RESET_DELAY_MS);

    /* 默认 10fps、关闭中断 */
    value = AMG8833_FPSC_10FPS;
    if (amg8833_write(handle, AMG8833_REG_FPSC, &value, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    value = 0x00U;
    if (amg8833_write(handle, AMG8833_REG_INTC, &value, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I(AMG8833_TAG, "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int amg8833_deinit(amg8833_handle_t *handle)
{
    uint8_t value = AMG8833_PCLT_STANDBY_10S;

    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)amg8833_write(handle, AMG8833_REG_PCLT, &value, 1U);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int amg8833_write(amg8833_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len)
{
    if (handle == NULL || handle->i2c.handle == NULL ||
        (data == NULL && len > 0U) || len == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    return driver_i2c_write_reg(&handle->i2c, reg, data, len);
}

int amg8833_read_reg(amg8833_handle_t *handle, uint8_t reg, uint8_t *data, size_t len)
{
    if (handle == NULL || handle->i2c.handle == NULL ||
        (data == NULL && len > 0U) || len == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, reg, data, len);
}

int amg8833_read_raw(amg8833_handle_t *handle, amg8833_raw_frame_t *frame)
{
    uint8_t buf[AMG8833_PIXEL_COUNT * 2U];
    uint8_t therm[2];
    uint8_t i;
    int ret;

    if (frame == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = amg8833_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = amg8833_read_reg(handle, AMG8833_REG_PIXEL_BASE, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }
    for (i = 0U; i < AMG8833_PIXEL_COUNT; i++) {
        frame->pixels[i] = driver_le16(&buf[i * 2U]);
    }

    ret = amg8833_read_reg(handle, AMG8833_REG_THML, therm, sizeof(therm));
    if (ret != DRIVER_OK) {
        return ret;
    }
    frame->thermistor = driver_le16(therm);
    return DRIVER_OK;
}

int amg8833_read(amg8833_handle_t *handle, float *pixels, float *thermistor_c)
{
    amg8833_raw_frame_t frame;
    uint8_t i;
    int ret;

    if (pixels == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = amg8833_read_raw(handle, &frame);
    if (ret != DRIVER_OK) {
        return ret;
    }

    for (i = 0U; i < AMG8833_PIXEL_COUNT; i++) {
        pixels[i] = amg8833_pixel_to_c(frame.pixels[i]);
    }
    if (thermistor_c != NULL) {
        *thermistor_c = (float)amg8833_therm_raw(frame.thermistor) * AMG8833_THERM_LSB_C;
    }
    return DRIVER_OK;
}

int amg8833_read_thermistor(amg8833_handle_t *handle, float *celsius)
{
    uint8_t therm[2];
    int ret;

    if (celsius == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = amg8833_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = amg8833_read_reg(handle, AMG8833_REG_THML, therm, sizeof(therm));
    if (ret != DRIVER_OK) {
        return ret;
    }
    *celsius = (float)amg8833_therm_raw(driver_le16(therm)) * AMG8833_THERM_LSB_C;
    return DRIVER_OK;
}

int amg8833_set_frame_rate(amg8833_handle_t *handle, amg8833_frame_rate_t rate)
{
    uint8_t value;
    int ret;

    ret = amg8833_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (rate != AMG8833_FRAME_RATE_10FPS && rate != AMG8833_FRAME_RATE_1FPS) {
        return (int)PORT_INVALID_PARAM;
    }

    value = (rate == AMG8833_FRAME_RATE_1FPS) ? AMG8833_FPSC_1FPS : AMG8833_FPSC_10FPS;
    ret = amg8833_write(handle, AMG8833_REG_FPSC, &value, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    handle->frame_rate = rate;
    return DRIVER_OK;
}

int amg8833_set_interrupt(amg8833_handle_t *handle, bool enable, float high_c, float low_c)
{
    uint16_t tth;
    uint8_t buf[2];
    uint8_t ttl;
    uint8_t intc;
    int ret;

    ret = amg8833_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (!enable) {
        intc = 0x00U;
        return amg8833_write(handle, AMG8833_REG_INTC, &intc, 1U);
    }

    /* 阈值范围检查：TTH 为 11 位有符号（0.0625℃/LSB），TTL 为 8 位（1℃/LSB） */
    if (high_c < 0.0f || high_c > 127.0f || low_c < 0.0f || low_c > 127.0f ||
        high_c <= low_c) {
        return (int)PORT_INVALID_PARAM;
    }

    tth = (uint16_t)(high_c / AMG8833_THERM_LSB_C) & 0x07FFU;
    buf[0] = (uint8_t)(tth & 0xFFU);
    buf[1] = (uint8_t)((tth >> 8) & 0x07U);
    ttl = (uint8_t)(low_c);
    intc = (uint8_t)(AMG8833_INTC_INTEN | AMG8833_INTC_INTMODE);

    ret = amg8833_write(handle, AMG8833_REG_INTC, &intc, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = amg8833_write(handle, AMG8833_REG_TTH, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }
    return amg8833_write(handle, AMG8833_REG_TTL, &ttl, 1U);
}

int amg8833_read_status(amg8833_handle_t *handle, uint8_t *status)
{
    uint8_t value = 0U;
    int ret;

    if (status == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = amg8833_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = amg8833_read_reg(handle, AMG8833_REG_STAT, &value, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    /* 写 1 清零 */
    (void)amg8833_write(handle, AMG8833_REG_SCLR, &value, 1U);
    *status = value;
    return DRIVER_OK;
}
