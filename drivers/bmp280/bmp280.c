#include "bmp280.h"
#include "bmp280_register.h"
#include "log.h"

static int bmp280_read_calib(bmp280_handle_t *handle)
{
    uint8_t d[BMP280_CALIB_DATA_SIZE];

    if (driver_i2c_read_reg(&handle->i2c, BMP280_REG_CALIB_00, d, sizeof(d)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    handle->calib.t1 = (uint16_t)((uint16_t)d[0] | ((uint16_t)d[1] << 8));
    handle->calib.t2 = (int16_t)((uint16_t)d[2] | ((uint16_t)d[3] << 8));
    handle->calib.t3 = (int16_t)((uint16_t)d[4] | ((uint16_t)d[5] << 8));

    handle->calib.p1 = (uint16_t)((uint16_t)d[6] | ((uint16_t)d[7] << 8));
    handle->calib.p2 = (int16_t)((uint16_t)d[8] | ((uint16_t)d[9] << 8));
    handle->calib.p3 = (int16_t)((uint16_t)d[10] | ((uint16_t)d[11] << 8));
    handle->calib.p4 = (int16_t)((uint16_t)d[12] | ((uint16_t)d[13] << 8));
    handle->calib.p5 = (int16_t)((uint16_t)d[14] | ((uint16_t)d[15] << 8));
    handle->calib.p6 = (int16_t)((uint16_t)d[16] | ((uint16_t)d[17] << 8));
    handle->calib.p7 = (int16_t)((uint16_t)d[18] | ((uint16_t)d[19] << 8));
    handle->calib.p8 = (int16_t)((uint16_t)d[20] | ((uint16_t)d[21] << 8));
    handle->calib.p9 = (int16_t)((uint16_t)d[22] | ((uint16_t)d[23] << 8));

    return DRIVER_OK;
}

/* Bosch 官方补偿算法：返回 0.01 ℃ */
static int32_t bmp280_compensate_temperature(bmp280_handle_t *handle, int32_t adc_t)
{
    int32_t var1;
    int32_t var2;
    const bmp280_calib_t *c = &handle->calib;
    int32_t t1 = (int32_t)c->t1;
    int32_t t2 = (int32_t)c->t2;
    int32_t t3 = (int32_t)c->t3;

    var1 = ((((adc_t >> 3) - (t1 << 1))) * t2) >> 11;
    var2 = (((((adc_t >> 4) - t1) * ((adc_t >> 4) - t1)) >> 12) * t3) >> 14;
    handle->t_fine = var1 + var2;
    return (handle->t_fine * 5 + 128) >> 8;
}

/* 返回 Pa；0 表示计算失败 */
static uint32_t bmp280_compensate_pressure(bmp280_handle_t *handle, int32_t adc_p)
{
    int32_t var1;
    int32_t var2;
    uint32_t p;
    const bmp280_calib_t *c = &handle->calib;

    var1 = ((int32_t)handle->t_fine >> 1) - (int32_t)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * (int32_t)c->p6;
    var2 = var2 + ((var1 * (int32_t)c->p5) << 1);
    var2 = (var2 >> 2) + (((int32_t)c->p4) << 16);
    var1 = ((((int32_t)c->p3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) +
            ((((int32_t)c->p2) * var1) >> 1)) >> 18;
    var1 = ((((32768 + var1)) * ((int32_t)c->p1)) >> 15);

    if (var1 == 0) {
        return 0U;
    }

    p = (uint32_t)(((int32_t)1048576 - adc_p) - (var2 >> 12)) * 3125U;
    if (p < 0x80000000U) {
        p = (p << 1) / (uint32_t)var1;
    } else {
        p = (p / (uint32_t)var1) * 2U;
    }
    return p;
}

int bmp280_init(bmp280_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t chip_id = 0U;
    uint8_t value;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : BMP280_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->t_fine = 0;
    handle->initialized = false;

    if (bmp280_get_chip_id(handle, &chip_id) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    if (chip_id != BMP280_CHIP_ID) {
        LOG_E("bmp280", "chip id mismatch: 0x%02X", chip_id);
        return (int)IOT_NOT_FOUND;
    }

    value = BMP280_RESET_VALUE;
    (void)driver_i2c_write_reg(&handle->i2c, BMP280_REG_RESET, &value, 1U);
    driver_delay_ms(5U);

    if (bmp280_read_calib(handle) != DRIVER_OK) {
        LOG_E("bmp280", "read calibration failed");
        return (int)IOT_IO_ERROR;
    }
    if (handle->calib.t1 == 0U || handle->calib.p1 == 0U) {
        LOG_E("bmp280", "invalid calibration data");
        return (int)IOT_INVALID_PARAM;
    }

    value = BMP280_CONFIG_DEFAULT;
    if (driver_i2c_write_reg(&handle->i2c, BMP280_REG_CONFIG, &value, 1U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    value = BMP280_CTRL_MEAS_DEFAULT;
    if (driver_i2c_write_reg(&handle->i2c, BMP280_REG_CTRL_MEAS, &value, 1U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("bmp280", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int bmp280_deinit(bmp280_handle_t *handle)
{
    uint8_t value = BMP280_MODE_SLEEP;

    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->i2c.handle != NULL) {
        (void)driver_i2c_write_reg(&handle->i2c, BMP280_REG_CTRL_MEAS, &value, 1U);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int bmp280_get_chip_id(bmp280_handle_t *handle, uint8_t *chip_id)
{
    if (handle == NULL || chip_id == NULL || handle->i2c.handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, BMP280_REG_CHIP_ID, chip_id, 1U);
}

int bmp280_read_raw(bmp280_handle_t *handle, int32_t *raw_temperature,
                    int32_t *raw_pressure)
{
    uint8_t d[6];
    int32_t adc_p;
    int32_t adc_t;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    if (driver_i2c_read_reg(&handle->i2c, BMP280_REG_PRESS_MSB, d, sizeof(d)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    adc_p = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | ((int32_t)d[2] >> 4);
    adc_t = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | ((int32_t)d[5] >> 4);

    if (raw_pressure != NULL) {
        *raw_pressure = adc_p;
    }
    if (raw_temperature != NULL) {
        *raw_temperature = adc_t;
    }
    return DRIVER_OK;
}

int bmp280_read(bmp280_handle_t *handle, bmp280_data_t *data)
{
    int32_t adc_p = 0;
    int32_t adc_t = 0;
    int32_t temperature;
    uint32_t pressure;
    int ret;

    if (data == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = bmp280_read_raw(handle, &adc_t, &adc_p);
    if (ret != DRIVER_OK) {
        return ret;
    }

    temperature = bmp280_compensate_temperature(handle, adc_t);
    pressure = bmp280_compensate_pressure(handle, adc_p);
    if (pressure == 0U) {
        return (int)IOT_ERROR;
    }

    data->temperature = (float)temperature / 100.0f;
    data->pressure = (float)pressure;
    return DRIVER_OK;
}
