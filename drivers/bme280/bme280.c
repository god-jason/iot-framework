#include "bme280.h"
#include "bme280_register.h"
#include "log.h"

/* 12 位补码符号扩展（H4 / H5 在寄存器中只占 12 位） */
static int16_t bme280_sign12(uint16_t value)
{
    if ((value & BME280_SIGN12_MASK) != 0U) {
        return (int16_t)(value | BME280_SIGN12_EXT);
    }
    return (int16_t)value;
}

static int bme280_read_calib(bme280_handle_t *handle)
{
    uint8_t tp[BME280_CALIB_DATA_SIZE];
    uint8_t h[BME280_CALIB_HUM_SIZE];
    bme280_calib_t *c = &handle->calib;

    if (hal_i2c_dev_read_reg(&handle->i2c, BME280_REG_CALIB_00, tp, sizeof(tp)) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (hal_i2c_dev_read_reg(&handle->i2c, BME280_REG_CALIB_HUM, h, sizeof(h)) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 0x88..0xA1：T1(u16) T2(i16) T3(i16) P1(u16) P2..P9(i16)，小端 */
    c->t1 = (uint16_t)hal_le16(&tp[0]);
    c->t2 = hal_le16(&tp[2]);
    c->t3 = hal_le16(&tp[4]);
    c->p1 = (uint16_t)hal_le16(&tp[6]);
    c->p2 = hal_le16(&tp[8]);
    c->p3 = hal_le16(&tp[10]);
    c->p4 = hal_le16(&tp[12]);
    c->p5 = hal_le16(&tp[14]);
    c->p6 = hal_le16(&tp[16]);
    c->p7 = hal_le16(&tp[18]);
    c->p8 = hal_le16(&tp[20]);
    c->p9 = hal_le16(&tp[22]);
    c->h1 = tp[24];

    /* 0xE1..0xE7：H2(12 位) H3(u8) H4(12 位) H5(12 位) H6(i8) */
    c->h2 = bme280_sign12((uint16_t)(((uint16_t)h[0] << 4) | ((uint16_t)h[1] >> 4)));
    c->h3 = h[2];
    c->h4 = bme280_sign12((uint16_t)(((uint16_t)h[3] << 4) | ((uint16_t)h[4] & 0x0FU)));
    c->h5 = bme280_sign12((uint16_t)(((uint16_t)h[5] << 4) | ((uint16_t)h[4] >> 4)));
    c->h6 = (int8_t)h[6];

    return HAL_OK;
}

/* 温度补偿：返回 0.01 ℃，同时保存 t_fine */
static int32_t bme280_compensate_temperature(bme280_handle_t *handle, int32_t adc_t)
{
    const bme280_calib_t *c = &handle->calib;
    int32_t var1;
    int32_t var2;
    int32_t t1 = (int32_t)c->t1;
    int32_t t2 = (int32_t)c->t2;
    int32_t t3 = (int32_t)c->t3;

    var1 = ((((adc_t >> 3) - (t1 << 1))) * t2) >> 11;
    var2 = (((((adc_t >> 4) - t1) * ((adc_t >> 4) - t1)) >> 12) * t3) >> 14;
    handle->t_fine = var1 + var2;

    return (handle->t_fine * 5 + 128) >> 8;
}

/* 压力补偿：返回 Pa，0 表示补偿失败 */
static uint32_t bme280_compensate_pressure(bme280_handle_t *handle, int32_t adc_p)
{
    const bme280_calib_t *c = &handle->calib;
    int32_t v1;
    int32_t v2;
    int64_t p;
    int64_t fine;

    v1 = ((int32_t)handle->t_fine >> 1) - 64000;
    v2 = (((v1 >> 2) * (v1 >> 2)) >> 11) * (int32_t)c->p6;
    v2 = v2 + ((v1 * (int32_t)c->p5) << 1);
    v2 = (v2 >> 2) + (((int32_t)c->p4) << 16);
    v1 = ((((int32_t)c->p3 * (((v1 >> 2) * (v1 >> 2)) >> 13)) >> 3) +
          ((((int32_t)c->p2) * v1) >> 1)) >> 18;
    v1 = (int32_t)((((int64_t)32768 + (int64_t)v1) * (int64_t)c->p1) >> 15);
    if (v1 == 0) {
        return 0U;
    }

    /* 一阶结果：p = ((2^20 - adc_p) - var2/2^12) * 6250 / var1，单位 Pa */
    p = (((int64_t)1048576 - (int64_t)adc_p) - ((int64_t)v2 >> 12));
    if (p <= 0) {
        return 0U;
    }
    p = (p * 6250) / (int64_t)v1;

    /* 二阶修正：p9*p^2/2^31 + p8*p/2^15 + p7，整体再除以 16 */
    fine = ((int64_t)c->p9 * p * p) / 2147483648LL;
    fine += ((int64_t)c->p8 * p) / 32768LL;
    fine += (int64_t)c->p7;
    p = p + (fine / 16);

    if (p <= 0) {
        return 0U;
    }
    return (uint32_t)p;
}

/* 湿度补偿：datasheet 双精度参考算法，返回 %RH */
static double bme280_compensate_humidity(bme280_handle_t *handle, int32_t adc_h)
{
    const bme280_calib_t *c = &handle->calib;
    double var_h;

    var_h = (double)handle->t_fine - 76800.0;
    var_h = ((double)adc_h - ((double)c->h4 * 64.0 + (double)c->h5 / 16384.0 * var_h)) *
            ((double)c->h2 / 65536.0 *
             (1.0 + (double)c->h6 / 67108864.0 * var_h *
              (1.0 + (double)c->h3 / 67108864.0 * var_h)));
    var_h = var_h * (1.0 - (double)c->h1 * var_h / 524288.0);

    if (var_h > 100.0) {
        var_h = 100.0;
    } else if (var_h < 0.0) {
        var_h = 0.0;
    }
    return var_h;
}

int bme280_init(bme280_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t chip_id = 0U;
    uint8_t value;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : BME280_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = HAL_I2C_TIMEOUT_DEFAULT;
    handle->t_fine = 0;
    handle->initialized = false;

    if (bme280_get_chip_id(handle, &chip_id) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (chip_id != BME280_CHIP_ID) {
        LOG_E("bme280", "chip id mismatch: 0x%02X", chip_id);
        return (int)HAL_NOT_FOUND;
    }

    value = BME280_RESET_VALUE;
    (void)hal_i2c_dev_write_reg(&handle->i2c, BME280_REG_RESET, &value, 1U);
    hal_delay_ms(BME280_RESET_DELAY_MS);

    if (bme280_read_calib(handle) != HAL_OK) {
        LOG_E("bme280", "read calibration failed");
        return (int)HAL_IO_ERROR;
    }
    if (handle->calib.t1 == 0U || handle->calib.p1 == 0U) {
        LOG_E("bme280", "invalid calibration data");
        return (int)HAL_INVALID_PARAM;
    }

    /* ctrl_hum 必须先写，再写 ctrl_meas 才生效 */
    value = BME280_CTRL_HUM_DEFAULT;
    if (hal_i2c_dev_write_reg(&handle->i2c, BME280_REG_CTRL_HUM, &value, 1U) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    value = BME280_CONFIG_DEFAULT;
    if (hal_i2c_dev_write_reg(&handle->i2c, BME280_REG_CONFIG, &value, 1U) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    value = BME280_CTRL_MEAS_DEFAULT;
    if (hal_i2c_dev_write_reg(&handle->i2c, BME280_REG_CTRL_MEAS, &value, 1U) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("bme280", "init ok at 0x%02X", handle->i2c.address);
    return HAL_OK;
}

int bme280_deinit(bme280_handle_t *handle)
{
    uint8_t value = BME280_MODE_SLEEP;

    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->i2c.handle != NULL) {
        (void)hal_i2c_dev_write_reg(&handle->i2c, BME280_REG_CTRL_MEAS, &value, 1U);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return HAL_OK;
}

int bme280_get_chip_id(bme280_handle_t *handle, uint8_t *chip_id)
{
    if (handle == NULL || chip_id == NULL || handle->i2c.handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return hal_i2c_dev_read_reg(&handle->i2c, BME280_REG_CHIP_ID, chip_id, 1U);
}

int bme280_read_raw(bme280_handle_t *handle, int32_t *raw_temperature,
                    int32_t *raw_pressure, int32_t *raw_humidity)
{
    uint8_t d[BME280_DATA_SIZE];
    int32_t adc_p;
    int32_t adc_t;
    int32_t adc_h;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (raw_temperature == NULL && raw_pressure == NULL && raw_humidity == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    if (hal_i2c_dev_read_reg(&handle->i2c, BME280_REG_DATA, d, sizeof(d)) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }

    adc_p = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | ((int32_t)d[2] >> 4);
    adc_t = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | ((int32_t)d[5] >> 4);
    adc_h = ((int32_t)d[6] << 8) | (int32_t)d[7];

    if (raw_pressure != NULL) {
        *raw_pressure = adc_p;
    }
    if (raw_temperature != NULL) {
        *raw_temperature = adc_t;
    }
    if (raw_humidity != NULL) {
        *raw_humidity = adc_h;
    }
    return HAL_OK;
}

int bme280_read(bme280_handle_t *handle, bme280_data_t *data)
{
    int32_t adc_t = 0;
    int32_t adc_p = 0;
    int32_t adc_h = 0;
    int32_t temp01;
    uint32_t pressure;
    double humidity;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = bme280_read_raw(handle, &adc_t, &adc_p, &adc_h);
    if (ret != HAL_OK) {
        return ret;
    }

    /* 温度先算（得到 t_fine），压力与湿度都依赖它 */
    temp01 = bme280_compensate_temperature(handle, adc_t);
    pressure = bme280_compensate_pressure(handle, adc_p);
    humidity = bme280_compensate_humidity(handle, adc_h);

    if (pressure < BME280_PRESSURE_MIN_PA || pressure > BME280_PRESSURE_MAX_PA) {
        LOG_W("bme280", "pressure out of range: %lu Pa", (unsigned long)pressure);
        return (int)HAL_ERROR;
    }

    data->temperature = (float)temp01 / 100.0f;
    data->pressure = (float)pressure;
    data->humidity = (float)humidity;
    return HAL_OK;
}
