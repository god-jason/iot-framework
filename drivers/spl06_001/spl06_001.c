#include "spl06_001.h"
#include "spl06_001_register.h"
#include "log.h"

/*
 * 过采样 -> 缩放因子（datasheet 表 9）：下标为 PRS_CFG.PM_PRC / TMP_CFG.TMP_PRC，
 * 对应过采样倍数 1/2/4/8/16/32/64/128。注意 16 倍以上因子重新从小值开始，
 * 这是芯片内部的移位规则，不是笔误。
 */
static const float spl06_001_scale_table[SPL06_001_SCALE_COUNT] = {
    524288.0f, 1572864.0f, 3670016.0f, 7864320.0f,
    253952.0f, 516096.0f, 1040384.0f, 2088960.0f
};

static int16_t spl06_001_sign12(uint16_t value)
{
    if ((value & SPL06_001_SIGN12_MASK) != 0U) {
        return (int16_t)(value | SPL06_001_SIGN12_EXT);
    }
    return (int16_t)value;
}

static int32_t spl06_001_sign20(uint32_t value)
{
    if ((value & SPL06_001_SIGN20_MASK) != 0U) {
        return (int32_t)(value | SPL06_001_SIGN20_EXT);
    }
    return (int32_t)value;
}

static int spl06_001_read_calib(spl06_001_handle_t *handle)
{
    uint8_t d[SPL06_001_COEF_SIZE];
    spl06_001_calib_t *c = &handle->calib;
    uint32_t u20;

    if (driver_i2c_read_reg(&handle->i2c, SPL06_001_REG_COEF, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 0x10: c0(12bit) c1(12bit) c00(20bit) c10(20bit) c01 c11 c20 c21 c30 */
    c->c0 = spl06_001_sign12((uint16_t)(((uint16_t)d[0] << 4) | ((uint16_t)d[1] >> 4)));
    c->c1 = spl06_001_sign12((uint16_t)((((uint16_t)d[1] & 0x0FU) << 8) | (uint16_t)d[2]));

    u20 = ((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | ((uint32_t)d[5] >> 4);
    c->c00 = spl06_001_sign20(u20);
    u20 = (((uint32_t)d[5] & 0x0FU) << 16) | ((uint32_t)d[6] << 8) | (uint32_t)d[7];
    c->c10 = spl06_001_sign20(u20);

    c->c01 = driver_be16(&d[8]);
    c->c11 = driver_be16(&d[10]);
    c->c20 = driver_be16(&d[12]);
    c->c21 = driver_be16(&d[14]);
    c->c30 = driver_be16(&d[16]);

    return DRIVER_OK;
}

static int spl06_001_wait_ready(spl06_001_handle_t *handle)
{
    uint32_t start = driver_tick_ms();
    uint8_t status = 0U;

    for (;;) {
        if (driver_i2c_read_reg(&handle->i2c, SPL06_001_REG_MEAS_CFG, &status, 1U) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        if ((status & (SPL06_001_STATUS_PRS_RDY | SPL06_001_STATUS_TMP_RDY)) ==
            (SPL06_001_STATUS_PRS_RDY | SPL06_001_STATUS_TMP_RDY)) {
            return DRIVER_OK;
        }
        if ((uint32_t)(driver_tick_ms() - start) > SPL06_001_READY_TIMEOUT_MS) {
            return (int)HAL_TIMEOUT;
        }
        driver_delay_ms(2U);
    }
}

int spl06_001_init(spl06_001_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t chip_id = 0U;
    uint8_t value;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : SPL06_001_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->kp = spl06_001_scale_table[SPL06_001_PRS_PRC_X8 & 0x07U];
    handle->kt = spl06_001_scale_table[SPL06_001_TMP_PRC_X8 & 0x07U];
    handle->initialized = false;

    if (spl06_001_get_chip_id(handle, &chip_id) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (chip_id != SPL06_001_CHIP_ID) {
        LOG_E("spl06", "chip id mismatch: 0x%02X", chip_id);
        return (int)HAL_NOT_FOUND;
    }

    value = SPL06_001_RESET_VALUE;
    (void)driver_i2c_write_reg(&handle->i2c, SPL06_001_REG_RESET, &value, 1U);
    driver_delay_ms(SPL06_001_RESET_DELAY_MS);

    if (spl06_001_read_calib(handle) != DRIVER_OK) {
        LOG_E("spl06", "read calibration failed");
        return (int)HAL_IO_ERROR;
    }
    if (handle->calib.c1 == 0 || handle->calib.c10 == 0) {
        LOG_E("spl06", "invalid calibration data");
        return (int)HAL_INVALID_PARAM;
    }

    /* 过采样与速率必须先配置，再启动连续测量 */
    value = SPL06_001_PRS_CFG_DEFAULT;
    if (driver_i2c_write_reg(&handle->i2c, SPL06_001_REG_PRS_CFG, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    value = SPL06_001_TMP_CFG_DEFAULT;
    if (driver_i2c_write_reg(&handle->i2c, SPL06_001_REG_TMP_CFG, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    value = SPL06_001_CFG_DEFAULT;
    if (driver_i2c_write_reg(&handle->i2c, SPL06_001_REG_CFG, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    value = SPL06_001_MEAS_CONT_PT;
    if (driver_i2c_write_reg(&handle->i2c, SPL06_001_REG_MEAS_CFG, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(SPL06_001_MEASURE_DELAY_MS);

    handle->initialized = true;
    LOG_I("spl06", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int spl06_001_deinit(spl06_001_handle_t *handle)
{
    uint8_t value = SPL06_001_MEAS_IDLE;

    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->i2c.handle != NULL) {
        (void)driver_i2c_write_reg(&handle->i2c, SPL06_001_REG_MEAS_CFG, &value, 1U);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int spl06_001_get_chip_id(spl06_001_handle_t *handle, uint8_t *chip_id)
{
    if (handle == NULL || chip_id == NULL || handle->i2c.handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, SPL06_001_REG_ID, chip_id, 1U);
}

int spl06_001_read_raw(spl06_001_handle_t *handle, int32_t *raw_temperature,
                       int32_t *raw_pressure)
{
    uint8_t d[SPL06_001_DATA_SIZE];
    int32_t press;
    int32_t temp;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (raw_temperature == NULL && raw_pressure == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = spl06_001_wait_ready(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 0x00..0x02 压力、0x03..0x05 温度，均为 24 位补码且 MSB 在前 */
    if (driver_i2c_read_reg(&handle->i2c, SPL06_001_REG_PSR_B2, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    press = ((int32_t)d[0] << 16) | ((int32_t)d[1] << 8) | (int32_t)d[2];
    if ((press & 0x00800000) != 0) {
        press |= (int32_t)0xFF000000;
    }
    temp = ((int32_t)d[3] << 16) | ((int32_t)d[4] << 8) | (int32_t)d[5];
    if ((temp & 0x00800000) != 0) {
        temp |= (int32_t)0xFF000000;
    }

    if (raw_pressure != NULL) {
        *raw_pressure = press;
    }
    if (raw_temperature != NULL) {
        *raw_temperature = temp;
    }
    return DRIVER_OK;
}

int spl06_001_read(spl06_001_handle_t *handle, spl06_001_data_t *data)
{
    const spl06_001_calib_t *c;
    int32_t raw_t = 0;
    int32_t raw_p = 0;
    float t_sc;
    float p_sc;
    float temperature;
    float pressure;
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->kp <= 0.0f || handle->kt <= 0.0f) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = spl06_001_read_raw(handle, &raw_t, &raw_p);
    if (ret != DRIVER_OK) {
        return ret;
    }

    c = &handle->calib;
    /* 原始值按过采样因子缩放到物理量纲 */
    t_sc = (float)raw_t / handle->kt;
    p_sc = (float)raw_p / handle->kp;

    /* datasheet 补偿多项式 */
    temperature = (float)c->c0 * 0.5f + (float)c->c1 * t_sc;
    pressure = (float)c->c00 +
               p_sc * ((float)c->c10 + p_sc * ((float)c->c20 + p_sc * (float)c->c30)) +
               t_sc * (float)c->c01 +
               t_sc * p_sc * ((float)c->c11 + p_sc * (float)c->c21);

    if (!(pressure >= SPL06_001_PRESSURE_MIN_PA && pressure <= SPL06_001_PRESSURE_MAX_PA)) {
        LOG_W("spl06", "pressure out of range: %ld Pa", (long)pressure);
        return (int)HAL_ERROR;
    }

    data->temperature = temperature;
    data->pressure = pressure;
    return DRIVER_OK;
}
