#include "tcs34725.h"
#include "tcs34725_register.h"
#include "log.h"

static uint8_t tcs34725_cmd(uint8_t reg, bool auto_inc)
{
    uint8_t cmd = (uint8_t)(TCS34725_CMD | reg);

    if (auto_inc) {
        cmd |= TCS34725_CMD_AUTO_INC;
    }
    return cmd;
}

static uint8_t tcs34725_gain_code(tcs34725_gain_t gain)
{
    switch (gain) {
    case TCS34725_GAIN_4X:
        return TCS34725_GAIN_4X_CODE;
    case TCS34725_GAIN_16X:
        return TCS34725_GAIN_16X_CODE;
    case TCS34725_GAIN_60X:
        return TCS34725_GAIN_60X_CODE;
    case TCS34725_GAIN_1X:
    default:
        return TCS34725_GAIN_1X_CODE;
    }
}

static float tcs34725_gain_factor(tcs34725_gain_t gain)
{
    switch (gain) {
    case TCS34725_GAIN_4X:
        return 4.0f;
    case TCS34725_GAIN_16X:
        return 16.0f;
    case TCS34725_GAIN_60X:
        return 60.0f;
    case TCS34725_GAIN_1X:
    default:
        return 1.0f;
    }
}

static uint8_t tcs34725_enable_value(bool rgbc)
{
    uint8_t value = TCS34725_ENABLE_PON;

    if (rgbc) {
        value |= TCS34725_ENABLE_AEN;
    }
    return value;
}

/* 满量程计数 = 积分周期数 * 每个周期的最大计数，超过 16 位则按 16 位饱和处理 */
static bool tcs34725_is_saturated(const tcs34725_handle_t *handle, uint16_t value)
{
    uint32_t full = (uint32_t)(256U - (uint32_t)handle->atime) * TCS34725_COUNT_PER_STEP;

    if (full > 0xFFFFU) {
        full = 0xFFFFU;
    }
    return ((uint32_t)value >= full);
}

static int tcs34725_write_reg8(tcs34725_handle_t *handle, uint8_t reg, uint8_t value)
{
    return driver_i2c_write_reg(&handle->i2c, tcs34725_cmd(reg, false), &value, 1U);
}

static uint32_t tcs34725_integration_delay_ms(const tcs34725_handle_t *handle)
{
    float time_ms = tcs34725_get_integration_time_ms(handle);

    return (uint32_t)time_ms + TCS34725_POWER_ON_DELAY_MS;
}

float tcs34725_get_integration_time_ms(const tcs34725_handle_t *handle)
{
    if (handle == NULL) {
        return 0.0f;
    }
    return (float)(256U - (uint32_t)handle->atime) * TCS34725_INTEGRATION_STEP_MS;
}

int tcs34725_init(tcs34725_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : TCS34725_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->atime = TCS34725_ATIME_DEFAULT;
    handle->gain = TCS34725_GAIN_4X;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("tcs34725", "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    ret = driver_i2c_read_reg(&handle->i2c, tcs34725_cmd(TCS34725_REG_ID, false), &id, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (id != TCS34725_ID_TCS34725 && id != TCS34725_ID_TCS34727) {
        LOG_E("tcs34725", "unexpected id 0x%02X", id);
        return (int)IOT_NOT_FOUND;
    }

    if (tcs34725_write_reg8(handle, TCS34725_REG_ATIME, handle->atime) != DRIVER_OK ||
        tcs34725_write_reg8(handle, TCS34725_REG_CONTROL,
                            tcs34725_gain_code(handle->gain)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    /* 先上电，等待振荡器稳定后再使能 RGBC */
    if (tcs34725_write_reg8(handle, TCS34725_REG_ENABLE,
                            tcs34725_enable_value(false)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(TCS34725_POWER_ON_DELAY_MS);

    if (tcs34725_write_reg8(handle, TCS34725_REG_ENABLE,
                            tcs34725_enable_value(true)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(tcs34725_integration_delay_ms(handle));

    handle->initialized = true;
    LOG_I("tcs34725", "init ok at 0x%02X (id=0x%02X)", handle->i2c.address, id);
    return DRIVER_OK;
}

int tcs34725_deinit(tcs34725_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->i2c.handle != NULL) {
        (void)tcs34725_write_reg8(handle, TCS34725_REG_ENABLE, 0x00U);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int tcs34725_set_atime(tcs34725_handle_t *handle, uint8_t atime)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    if (tcs34725_write_reg8(handle, TCS34725_REG_ATIME, atime) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    handle->atime = atime;
    driver_delay_ms(tcs34725_integration_delay_ms(handle));
    return DRIVER_OK;
}

int tcs34725_set_gain(tcs34725_handle_t *handle, tcs34725_gain_t gain)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if ((unsigned int)gain > (unsigned int)TCS34725_GAIN_60X) {
        return (int)IOT_INVALID_PARAM;
    }

    if (tcs34725_write_reg8(handle, TCS34725_REG_CONTROL, tcs34725_gain_code(gain)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    handle->gain = gain;
    driver_delay_ms(tcs34725_integration_delay_ms(handle));
    return DRIVER_OK;
}

int tcs34725_read_raw(tcs34725_handle_t *handle, tcs34725_rgbc_t *rgbc)
{
    uint8_t buf[8] = { 0U };
    int ret;

    if (handle == NULL || rgbc == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    /* 等待一个完整积分周期后数据才有效 */
    driver_delay_ms(tcs34725_integration_delay_ms(handle));

    /* 一次读回 C/R/G/B 共 8 字节，寄存器内为小端 */
    ret = driver_i2c_read_reg(&handle->i2c, tcs34725_cmd(TCS34725_REG_CDATAL, true),
                              buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }

    rgbc->clear = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
    rgbc->red   = (uint16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
    rgbc->green = (uint16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
    rgbc->blue  = (uint16_t)((uint16_t)buf[6] | ((uint16_t)buf[7] << 8));
    rgbc->saturated = tcs34725_is_saturated(handle, rgbc->clear) ||
                      tcs34725_is_saturated(handle, rgbc->red) ||
                      tcs34725_is_saturated(handle, rgbc->green) ||
                      tcs34725_is_saturated(handle, rgbc->blue);
    return DRIVER_OK;
}

int tcs34725_read(tcs34725_handle_t *handle, tcs34725_data_t *data)
{
    float norm;
    float red;
    float green;
    float blue;
    int ret;

    if (handle == NULL || data == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = tcs34725_read_raw(handle, &data->rgbc);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 归一化到参考积分时间与 1x 增益后再套用经验系数（需按标定修正） */
    norm = TCS34725_LUX_REF_TIME_MS / tcs34725_get_integration_time_ms(handle);
    norm /= tcs34725_gain_factor(handle->gain);

    red = (float)data->rgbc.red * norm;
    green = (float)data->rgbc.green * norm;
    blue = (float)data->rgbc.blue * norm;

    data->lux = (TCS34725_LUX_COEF_R * red) + (TCS34725_LUX_COEF_G * green) +
                (TCS34725_LUX_COEF_B * blue);
    if (data->lux < 0.0f) {
        data->lux = 0.0f;
    }
    return DRIVER_OK;
}
