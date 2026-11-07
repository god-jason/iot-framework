#include "bmp180.h"
#include "bmp180_register.h"
#include "iot_logger.h"

/* 压力转换时间，下标为 oss（0..3） */
static const uint32_t bmp180_press_delay_ms[BMP180_OSS_COUNT] = { 5U, 8U, 14U, 26U };

static int bmp180_read_calib(bmp180_handle_t *handle)
{
    uint8_t d[BMP180_CALIB_DATA_SIZE];

    if (driver_i2c_read_reg(&handle->i2c, BMP180_REG_CALIB_00, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    /* 11 个系数均为 16 位大端 */
    handle->calib.ac1 = driver_be16(&d[0]);
    handle->calib.ac2 = driver_be16(&d[2]);
    handle->calib.ac3 = driver_be16(&d[4]);
    handle->calib.ac4 = (uint16_t)driver_be16(&d[6]);
    handle->calib.ac5 = (uint16_t)driver_be16(&d[8]);
    handle->calib.ac6 = (uint16_t)driver_be16(&d[10]);
    handle->calib.b1 = driver_be16(&d[12]);
    handle->calib.b2 = driver_be16(&d[14]);
    handle->calib.mb = driver_be16(&d[16]);
    handle->calib.mc = driver_be16(&d[18]);
    handle->calib.md = driver_be16(&d[20]);

    return DRIVER_OK;
}

static int bmp180_read_ut(bmp180_handle_t *handle, int32_t *ut)
{
    uint8_t cmd = BMP180_CMD_TEMP;
    uint8_t d[BMP180_ADC_TEMP_SIZE];

    if (driver_i2c_write(&handle->i2c, &cmd, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(BMP180_CONV_DELAY_TEMP_MS);

    if (driver_i2c_read_reg(&handle->i2c, BMP180_REG_ADC_OUT, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    *ut = (int32_t)(((uint32_t)d[0] << 8) | (uint32_t)d[1]);
    return DRIVER_OK;
}

static int bmp180_read_up(bmp180_handle_t *handle, int32_t *up)
{
    uint8_t cmd = BMP180_CMD_PRESS(handle->oversampling);
    uint8_t d[BMP180_ADC_PRESS_SIZE];
    int32_t raw;

    if (driver_i2c_write(&handle->i2c, &cmd, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(bmp180_press_delay_ms[handle->oversampling & 0x03U]);

    if (driver_i2c_read_reg(&handle->i2c, BMP180_REG_ADC_OUT, d, sizeof(d)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    /* 结果 19 位左对齐，右移 (8 - oss) 得到 UP */
    raw = (int32_t)(((uint32_t)d[0] << 16) | ((uint32_t)d[1] << 8) | (uint32_t)d[2]);
    *up = raw >> (8 - (int32_t)(handle->oversampling & 0x03U));
    return DRIVER_OK;
}

/* datasheet 温度补偿：返回 0.1 ℃，并保存 B5 供压力补偿使用 */
static int32_t bmp180_compensate_temperature(bmp180_handle_t *handle, int32_t ut)
{
    const bmp180_calib_t *c = &handle->calib;
    int64_t x1;
    int64_t x2;
    int64_t denom;

    /* X1 = (UT - AC6) * AC5 / 2^15，用 64 位避免乘积溢出 */
    x1 = ((int64_t)(ut - (int32_t)c->ac6) * (int64_t)c->ac5) >> 15;
    denom = x1 + (int64_t)c->md;
    if (denom == 0) {
        handle->b5 = 0;
        return 0;
    }
    x2 = ((int64_t)c->mc << 11) / denom;
    handle->b5 = (int32_t)(x1 + x2);

    return (handle->b5 + 8) >> 4;
}

/* datasheet 压力补偿：返回 Pa，0 表示补偿失败 */
static uint32_t bmp180_compensate_pressure(bmp180_handle_t *handle, int32_t up)
{
    const bmp180_calib_t *c = &handle->calib;
    int64_t x1;
    int64_t x2;
    int64_t x3;
    int64_t b3;
    int64_t b4;
    int64_t b6;
    int64_t b7;
    int64_t p;
    uint8_t oss = (uint8_t)(handle->oversampling & 0x03U);

    b6 = (int64_t)handle->b5 - 4000;
    x1 = ((int64_t)c->b2 * ((b6 * b6) >> 12)) >> 11;
    x2 = ((int64_t)c->ac2 * b6) >> 11;
    x3 = x1 + x2;
    b3 = ((((int64_t)c->ac1 * 4 + x3) << oss) + 2) >> 2;

    x1 = ((int64_t)c->ac3 * b6) >> 13;
    x2 = ((int64_t)c->b1 * ((b6 * b6) >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    b4 = ((int64_t)c->ac4 * (x3 + 32768)) >> 15;
    if (b4 <= 0) {
        return 0U;
    }

    b7 = ((int64_t)up - b3) * (int64_t)(50000UL >> oss);
    if (b7 < 0) {
        return 0U;
    }
    if (b7 < 0x80000000LL) {
        p = (b7 << 1) / b4;
    } else {
        p = (b7 / b4) << 1;
    }

    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    p = p + ((x1 + x2 + 3791) >> 4);
    if (p <= 0) {
        return 0U;
    }
    return (uint32_t)p;
}

int bmp180_init(bmp180_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint8_t chip_id = 0U;
    uint8_t value = BMP180_RESET_VALUE;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : BMP180_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->oversampling = BMP180_OSS_DEFAULT;
    handle->b5 = 0;
    handle->initialized = false;

    if (bmp180_get_chip_id(handle, &chip_id) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (chip_id != BMP180_CHIP_ID) {
        LOG_E("bmp180", "chip id mismatch: 0x%02X", chip_id);
        return (int)PORT_NOT_FOUND;
    }

    (void)driver_i2c_write_reg(&handle->i2c, BMP180_REG_SOFT_RESET, &value, 1U);
    driver_delay_ms(5U);

    if (bmp180_read_calib(handle) != DRIVER_OK) {
        LOG_E("bmp180", "read calibration failed");
        return (int)PORT_IO_ERROR;
    }
    if (handle->calib.ac5 == 0U || handle->calib.ac6 == 0U || handle->calib.md == 0) {
        LOG_E("bmp180", "invalid calibration data");
        return (int)PORT_INVALID_PARAM;
    }

    handle->initialized = true;
    LOG_I("bmp180", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int bmp180_deinit(bmp180_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int bmp180_set_oversampling(bmp180_handle_t *handle, uint8_t oss)
{
    if (handle == NULL || !handle->initialized || oss >= BMP180_OSS_COUNT) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->oversampling = oss;
    return DRIVER_OK;
}

int bmp180_get_chip_id(bmp180_handle_t *handle, uint8_t *chip_id)
{
    if (handle == NULL || chip_id == NULL || handle->i2c.handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, BMP180_REG_CHIP_ID, chip_id, 1U);
}

int bmp180_read_raw(bmp180_handle_t *handle, int32_t *ut, int32_t *up)
{
    int32_t raw_t = 0;
    int32_t raw_p = 0;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (ut == NULL && up == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = bmp180_read_ut(handle, &raw_t);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = bmp180_read_up(handle, &raw_p);
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (ut != NULL) {
        *ut = raw_t;
    }
    if (up != NULL) {
        *up = raw_p;
    }
    return DRIVER_OK;
}

int bmp180_read(bmp180_handle_t *handle, bmp180_data_t *data)
{
    int32_t ut = 0;
    int32_t up = 0;
    int32_t temp01;
    uint32_t pressure;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = bmp180_read_raw(handle, &ut, &up);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 温度必须先算，压力补偿依赖 B5 */
    temp01 = bmp180_compensate_temperature(handle, ut);
    pressure = bmp180_compensate_pressure(handle, up);
    if (pressure < BMP180_PRESSURE_MIN_PA || pressure > BMP180_PRESSURE_MAX_PA) {
        LOG_W("bmp180", "pressure out of range: %lu Pa", (unsigned long)pressure);
        return (int)PORT_ERROR;
    }

    data->temperature = (float)temp01 / 10.0f;
    data->pressure = (float)pressure;
    return DRIVER_OK;
}
