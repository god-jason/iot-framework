#include "bme680.h"
#include "bme680_register.h"
#include "log.h"

static int bme680_read_calib(bme680_handle_t *handle)
{
    uint8_t d1[BME680_CALIB1_SIZE];
    uint8_t d2[BME680_CALIB2_SIZE];
    bme680_calib_t *c = &handle->calib;

    if (driver_i2c_read_reg(&handle->i2c, BME680_REG_CALIB_00, d1, sizeof(d1)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (driver_i2c_read_reg(&handle->i2c, BME680_REG_CALIB_01, d2, sizeof(d2)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 0x89..0x9C：d1[0] 保留，其余 T2/T3/P1..P10，均为小端 */
    c->t2 = driver_le16(&d1[1]);
    c->t3 = (int8_t)d1[3];
    c->p1 = (uint16_t)driver_le16(&d1[4]);
    c->p2 = driver_le16(&d1[6]);
    c->p3 = (int8_t)d1[8];
    c->p4 = driver_le16(&d1[9]);
    c->p5 = driver_le16(&d1[11]);
    c->p6 = (int8_t)d1[13];
    c->p7 = (int8_t)d1[14];
    c->p8 = driver_le16(&d1[15]);
    c->p9 = driver_le16(&d1[17]);
    c->p10 = d1[19];

    /* 0xE1..0xEA：H2(0xE1 + 0xE2 高 4 位) H1(0xE2 低 4 位 + 0xE3)
     * H3..H7 各 1 字节，T1 为 0xE9/0xEA 小端 */
    c->h2 = (int16_t)(((uint16_t)d2[0] << 4) | ((uint16_t)d2[1] >> 4));
    c->h1 = (uint16_t)((((uint16_t)d2[1] & 0x0FU) << 8) | (uint16_t)d2[2]);
    c->h3 = (int8_t)d2[3];
    c->h4 = (int8_t)d2[4];
    c->h5 = (int8_t)d2[5];
    c->h6 = (int8_t)d2[6];
    c->h7 = (int8_t)d2[7];
    c->t1 = (uint16_t)driver_le16(&d2[8]);

    return DRIVER_OK;
}

/* 温度补偿：返回 ℃，同时保存 t_fine（双精度参考算法） */
static double bme680_compensate_temperature(bme680_handle_t *handle, int32_t adc_t)
{
    const bme680_calib_t *c = &handle->calib;
    double var1;
    double var2;

    var1 = ((double)adc_t / 16384.0 - (double)c->t1 / 1024.0) * (double)c->t2;
    var2 = (double)adc_t / 131072.0 - (double)c->t1 / 8192.0;
    var2 = var2 * var2 * (double)c->t3;
    handle->t_fine = var1 + var2;

    return handle->t_fine / 5120.0;
}

/* 压力补偿：返回 Pa，0 表示补偿失败 */
static double bme680_compensate_pressure(bme680_handle_t *handle, int32_t adc_p)
{
    const bme680_calib_t *c = &handle->calib;
    double var1;
    double var2;
    double p;

    var1 = (handle->t_fine / 2.0) - 64000.0;
    var2 = var1 * var1 * (double)c->p6 / 131072.0;
    var2 = var2 + (var1 * (double)c->p5 * 2.0);
    var2 = (var2 / 4.0) + ((double)c->p4 * 65536.0);
    var1 = (((double)c->p3 * var1 * var1) / 16384.0) + ((double)c->p2 * var1);
    var1 = var1 / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * (double)c->p1;
    if (var1 == 0.0) {
        return 0.0;
    }

    p = 1048576.0 - (double)adc_p;
    p = ((p - (var2 / 4096.0)) * 6250.0) / var1;

    var1 = ((double)c->p9 * p * p) / 2147483648.0;
    var2 = (double)c->p8 * p / 32768.0;

    return p + ((var1 + var2 + (double)c->p7) / 16.0);
}

/* 湿度补偿：返回 %RH（已限幅到 0..100） */
static double bme680_compensate_humidity(bme680_handle_t *handle, int32_t adc_h)
{
    const bme680_calib_t *c = &handle->calib;
    double temperature = handle->t_fine / 5120.0;   /* ℃ */
    double var1;
    double var2;
    double var3;
    double var4;
    double hum;

    var1 = (double)adc_h - ((double)c->h1 * 16.0 + (double)c->h3 / 2.0 * temperature);
    var2 = var1 * ((double)c->h2 / 262144.0 *
                   (1.0 + (double)c->h4 / 16384.0 * temperature +
                    (double)c->h5 / 1048576.0 * temperature * temperature));
    var3 = (double)c->h6 / 16384.0;
    var4 = (double)c->h7 / 2097152.0;
    hum = var2 + ((var3 + (var4 * temperature)) * var2 * var2);

    if (hum > 100.0) {
        hum = 100.0;
    } else if (hum < 0.0) {
        hum = 0.0;
    }
    return hum;
}

static int bme680_trigger(bme680_handle_t *handle)
{
    uint8_t value = BME680_CTRL_MEAS_FORCED;

    if (driver_i2c_write_reg(&handle->i2c, BME680_REG_CTRL_MEAS, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(BME680_MEASURE_DELAY_MS);
    return DRIVER_OK;
}

int bme680_init(bme680_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t chip_id = 0U;
    uint8_t value;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : BME680_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->t_fine = 0.0;
    handle->initialized = false;

    if (bme680_get_chip_id(handle, &chip_id) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (chip_id != BME680_CHIP_ID) {
        LOG_E("bme680", "chip id mismatch: 0x%02X", chip_id);
        return (int)HAL_NOT_FOUND;
    }

    value = BME680_RESET_VALUE;
    (void)driver_i2c_write_reg(&handle->i2c, BME680_REG_RESET, &value, 1U);
    driver_delay_ms(BME680_RESET_DELAY_MS);

    if (bme680_read_calib(handle) != DRIVER_OK) {
        LOG_E("bme680", "read calibration failed");
        return (int)HAL_IO_ERROR;
    }
    if (handle->calib.t2 == 0 || handle->calib.p1 == 0U) {
        LOG_E("bme680", "invalid calibration data");
        return (int)HAL_INVALID_PARAM;
    }

    value = BME680_CTRL_GAS1_OFF;
    if (driver_i2c_write_reg(&handle->i2c, BME680_REG_CTRL_GAS_1, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    value = BME680_CTRL_HUM_DEFAULT;
    if (driver_i2c_write_reg(&handle->i2c, BME680_REG_CTRL_HUM, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    value = BME680_CONFIG_DEFAULT;
    if (driver_i2c_write_reg(&handle->i2c, BME680_REG_CONFIG, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    value = BME680_MODE_SLEEP;
    if (driver_i2c_write_reg(&handle->i2c, BME680_REG_CTRL_MEAS, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("bme680", "init ok at 0x%02X (gas unsupported)", handle->i2c.address);
    return DRIVER_OK;
}

int bme680_deinit(bme680_handle_t *handle)
{
    uint8_t value = BME680_MODE_SLEEP;

    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->i2c.handle != NULL) {
        (void)driver_i2c_write_reg(&handle->i2c, BME680_REG_CTRL_MEAS, &value, 1U);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int bme680_get_chip_id(bme680_handle_t *handle, uint8_t *chip_id)
{
    if (handle == NULL || chip_id == NULL || handle->i2c.handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, BME680_REG_CHIP_ID, chip_id, 1U);
}

int bme680_read_raw(bme680_handle_t *handle, int32_t *raw_temperature,
                    int32_t *raw_pressure, int32_t *raw_humidity)
{
    uint8_t d[BME680_DATA_SIZE];
    int32_t adc_p;
    int32_t adc_t;
    int32_t adc_h;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (raw_temperature == NULL && raw_pressure == NULL && raw_humidity == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = bme680_trigger(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    if (driver_i2c_read_reg(&handle->i2c, BME680_REG_DATA, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 0x1D..0x1F 压力 20 位，0x20..0x22 温度 20 位，0x23/0x24 湿度 */
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
    return DRIVER_OK;
}

int bme680_read(bme680_handle_t *handle, bme680_data_t *data)
{
    int32_t adc_t = 0;
    int32_t adc_p = 0;
    int32_t adc_h = 0;
    double temperature;
    double pressure;
    double humidity;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = bme680_read_raw(handle, &adc_t, &adc_p, &adc_h);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 温度先算（得到 t_fine），压力与湿度都依赖它 */
    temperature = bme680_compensate_temperature(handle, adc_t);
    pressure = bme680_compensate_pressure(handle, adc_p);
    humidity = bme680_compensate_humidity(handle, adc_h);

    if (!(pressure >= (double)BME680_PRESSURE_MIN_PA &&
          pressure <= (double)BME680_PRESSURE_MAX_PA)) {
        LOG_W("bme680", "pressure out of range");
        return (int)HAL_ERROR;
    }

    data->temperature = (float)temperature;
    data->pressure = (float)pressure;
    data->humidity = (float)humidity;
    data->gas_resistance = 0.0f;   /* 气体测量未实现 */
    return DRIVER_OK;
}
