#include "bmp390.h"
#include "bmp390_register.h"
#include "log.h"

#define BMP390_SPI_XFER_MAX         32U

/* ---------------- 总线访问（I2C / SPI 统一） ---------------- */

static uint16_t bmp390_le16u(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[1] << 8) | (uint16_t)p[0]);
}

static int bmp390_bus_read(bmp390_handle_t *handle, uint8_t reg, uint8_t *data, size_t len)
{
    uint8_t tx[BMP390_SPI_XFER_MAX];
    uint8_t rx[BMP390_SPI_XFER_MAX];
    size_t i;
    int ret;

    if (!handle->use_spi) {
        return driver_i2c_read_reg(&handle->i2c, reg, data, len);
    }
    if (data == NULL || len == 0U || (len + 1U) > BMP390_SPI_XFER_MAX) {
        return (int)HAL_INVALID_PARAM;
    }

    tx[0] = (uint8_t)(reg | 0x80U);              /* SPI 读：地址最高位置 1 */
    for (i = 1U; i <= len; i++) {
        tx[i] = 0xFFU;
    }

    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, rx, len + 1U);
    driver_spi_deselect(&handle->spi);
    if (ret != DRIVER_OK) {
        return ret;
    }
    for (i = 0U; i < len; i++) {
        data[i] = rx[i + 1U];
    }
    return DRIVER_OK;
}

static int bmp390_bus_write(bmp390_handle_t *handle, uint8_t reg,
                            const uint8_t *data, size_t len)
{
    uint8_t tx[BMP390_SPI_XFER_MAX];
    size_t i;

    if (!handle->use_spi) {
        return driver_i2c_write_reg(&handle->i2c, reg, data, len);
    }
    if (data == NULL || len == 0U || (len + 1U) > BMP390_SPI_XFER_MAX) {
        return (int)HAL_INVALID_PARAM;
    }

    tx[0] = (uint8_t)(reg & 0x7FU);              /* SPI 写：地址最高位清 0 */
    for (i = 0U; i < len; i++) {
        tx[i + 1U] = data[i];
    }
    return driver_spi_write_cs(&handle->spi, tx, len + 1U);
}

/* ---------------- 校准系数 ---------------- */

static int bmp390_read_calib(bmp390_handle_t *handle)
{
    uint8_t d[BMP390_CALIB_DATA_SIZE];
    bmp390_calib_t *c = &handle->calib;

    if (bmp390_bus_read(handle, BMP390_REG_CALIB, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 0x31 起：T1(u16) T2(u16) T3(i8) P1(i16) P2(i16) P3(i8) P4(i8)
     * P5(u16) P6(u16) P7(i8) P8(i8) P9(i8) P10(i8) P11(i8)，均小端 */
    c->t1 = (double)bmp390_le16u(&d[0]) * BMP390_CAL_T1_SCALE;
    c->t2 = (double)bmp390_le16u(&d[2]) * BMP390_CAL_T2_SCALE;
    c->t3 = (double)(int8_t)d[4] * BMP390_CAL_T3_SCALE;
    c->p1 = (double)driver_le16(&d[5]) * BMP390_CAL_P1_SCALE;
    c->p2 = (double)driver_le16(&d[7]) * BMP390_CAL_P2_SCALE;
    c->p3 = (double)(int8_t)d[9] * BMP390_CAL_P3_SCALE;
    c->p4 = (double)(int8_t)d[10] * BMP390_CAL_P4_SCALE;
    c->p5 = (double)bmp390_le16u(&d[11]) * BMP390_CAL_P5_SCALE;
    c->p6 = (double)bmp390_le16u(&d[13]) * BMP390_CAL_P6_SCALE;
    c->p7 = (double)(int8_t)d[15] * BMP390_CAL_P7_SCALE;
    c->p8 = (double)(int8_t)d[16] * BMP390_CAL_P8_SCALE;
    c->p9 = (double)(int8_t)d[17] * BMP390_CAL_P9_SCALE;
    c->p10 = (double)(int8_t)d[18] * BMP390_CAL_P10_SCALE;
    c->p11 = (double)(int8_t)d[19] * BMP390_CAL_P11_SCALE;
    return DRIVER_OK;
}

/* ---------------- 补偿 ---------------- */

/* 温度：t_lin = T2*(UT - T1) + T3*(UT - T1)^2，单位 ℃ */
static double bmp390_compensate_temperature(bmp390_handle_t *handle, uint32_t adc_t)
{
    double dt = (double)adc_t - handle->calib.t1;

    handle->t_lin = handle->calib.t2 * dt + handle->calib.t3 * dt * dt;
    return handle->t_lin;
}

/* 压力：P = P5 + P6*T + P7*T^2 + P8*T^3
 *            + UP*(P1 + P2*T + P3*T^2 + P4*T^3)
 *            + UP^2*(P9 + P10*T) + UP^3*P11，单位 Pa */
static double bmp390_compensate_pressure(bmp390_handle_t *handle, uint32_t adc_p)
{
    const bmp390_calib_t *c = &handle->calib;
    double t = handle->t_lin;
    double pu = (double)adc_p;
    double t2 = t * t;
    double out1 = c->p5 + (c->p6 * t) + (c->p7 * t2) + (c->p8 * t2 * t);
    double out2 = pu * (c->p1 + (c->p2 * t) + (c->p3 * t2) + (c->p4 * t2 * t));
    double out3 = (pu * pu) * (c->p9 + (c->p10 * t)) + (pu * pu * pu) * c->p11;

    return out1 + out2 + out3;
}

/* ---------------- 公共流程 ---------------- */

static int bmp390_setup(bmp390_handle_t *handle)
{
    uint8_t chip_id = 0U;
    uint8_t value;

    if (bmp390_get_chip_id(handle, &chip_id) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (chip_id != BMP390_CHIP_ID) {
        LOG_E("bmp390", "chip id mismatch: 0x%02X", chip_id);
        return (int)HAL_NOT_FOUND;
    }

    value = BMP390_CMD_SOFT_RESET;
    (void)bmp390_bus_write(handle, BMP390_REG_CMD, &value, 1U);
    driver_delay_ms(BMP390_RESET_DELAY_MS);

    if (bmp390_read_calib(handle) != DRIVER_OK) {
        LOG_E("bmp390", "read calibration failed");
        return (int)HAL_IO_ERROR;
    }
    if (handle->calib.t1 == 0.0 || handle->calib.t2 == 0.0 || handle->calib.p1 == 0.0) {
        LOG_E("bmp390", "invalid calibration data");
        return (int)HAL_INVALID_PARAM;
    }

    value = BMP390_CONFIG_DEFAULT;
    if (bmp390_bus_write(handle, BMP390_REG_CONFIG, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    value = BMP390_OSR_DEFAULT;
    if (bmp390_bus_write(handle, BMP390_REG_OSR, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    value = BMP390_PWR_CTRL_NORMAL;
    if (bmp390_bus_write(handle, BMP390_REG_PWR_CTRL, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->t_lin = 0.0;
    handle->initialized = true;
    LOG_I("bmp390", "init ok (%s)", handle->use_spi ? "spi" : "i2c");
    return DRIVER_OK;
}

static void bmp390_reset_bus(bmp390_handle_t *handle, bool use_spi)
{
    handle->use_spi = use_spi;
    handle->i2c.handle = NULL;
    handle->i2c.address = BMP390_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->spi.handle = NULL;
    handle->spi.cs = 0U;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->initialized = false;
}

int bmp390_init(bmp390_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    bmp390_reset_bus(handle, false);
    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : BMP390_DEFAULT_ADDRESS;
    return bmp390_setup(handle);
}

int bmp390_init_spi(bmp390_handle_t *handle, hal_spi_t *spi, hal_pin_t cs)
{
    if (handle == NULL || spi == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    bmp390_reset_bus(handle, true);
    handle->spi.handle = spi;
    handle->spi.cs = cs;
    return bmp390_setup(handle);
}

int bmp390_deinit(bmp390_handle_t *handle)
{
    uint8_t value = 0x00U;   /* pwr_ctrl = sleep */

    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)bmp390_bus_write(handle, BMP390_REG_PWR_CTRL, &value, 1U);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    handle->spi.handle = NULL;
    return DRIVER_OK;
}

int bmp390_get_chip_id(bmp390_handle_t *handle, uint8_t *chip_id)
{
    if (handle == NULL || chip_id == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->use_spi ? (handle->spi.handle == NULL) : (handle->i2c.handle == NULL)) {
        return (int)HAL_INVALID_PARAM;
    }
    return bmp390_bus_read(handle, BMP390_REG_CHIP_ID, chip_id, 1U);
}

int bmp390_read_raw(bmp390_handle_t *handle, uint32_t *raw_temperature,
                    uint32_t *raw_pressure)
{
    uint8_t d[BMP390_DATA_SIZE];
    uint32_t press;
    uint32_t temp;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (raw_temperature == NULL && raw_pressure == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (bmp390_bus_read(handle, BMP390_REG_DATA_0, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 小端、XLSB 在前 */
    press = ((uint32_t)d[2] << 16) | ((uint32_t)d[1] << 8) | (uint32_t)d[0];
    temp = ((uint32_t)d[5] << 16) | ((uint32_t)d[4] << 8) | (uint32_t)d[3];
    if (raw_pressure != NULL) {
        *raw_pressure = press;
    }
    if (raw_temperature != NULL) {
        *raw_temperature = temp;
    }
    return DRIVER_OK;
}

int bmp390_read(bmp390_handle_t *handle, bmp390_data_t *data)
{
    uint32_t raw_t = 0U;
    uint32_t raw_p = 0U;
    double temperature;
    double pressure;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = bmp390_read_raw(handle, &raw_t, &raw_p);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 温度先算，压力补偿依赖 t_lin */
    temperature = bmp390_compensate_temperature(handle, raw_t);
    pressure = bmp390_compensate_pressure(handle, raw_p);

    /* 用取反形式判断，可同时拦住 NaN */
    if (!(pressure >= (double)BMP390_PRESSURE_MIN_PA &&
          pressure <= (double)BMP390_PRESSURE_MAX_PA)) {
        LOG_W("bmp390", "pressure out of range");
        return (int)HAL_ERROR;
    }

    data->temperature = (float)temperature;
    data->pressure = (float)pressure;
    return DRIVER_OK;
}
