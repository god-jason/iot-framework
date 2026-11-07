#include "hmc5883l.h"
#include "hmc5883l_register.h"
#include "log.h"
#include <math.h>

/* 按增益档位索引：LSB/Gauss */
static const float s_lsb_per_gauss[HMC5883L_GAIN_COUNT] = {
    HMC5883L_GAIN_1_3GA_LSB, HMC5883L_GAIN_1_9GA_LSB, HMC5883L_GAIN_2_5GA_LSB,
    HMC5883L_GAIN_4_0GA_LSB, HMC5883L_GAIN_4_7GA_LSB, HMC5883L_GAIN_5_6GA_LSB,
    HMC5883L_GAIN_8_1GA_LSB
};

static int hmc5883l_write(hmc5883l_handle_t *handle, uint8_t reg, uint8_t value)
{
    return (driver_i2c_write_reg(&handle->i2c, reg, &value, 1U) == DRIVER_OK) ? DRIVER_OK
                                                                              : (int)IOT_IO_ERROR;
}

static int hmc5883l_read_reg(hmc5883l_handle_t *handle, uint8_t reg, uint8_t *buf, size_t len)
{
    return (driver_i2c_read_reg(&handle->i2c, reg, buf, len) == DRIVER_OK) ? DRIVER_OK
                                                                          : (int)IOT_IO_ERROR;
}

int hmc5883l_get_status(hmc5883l_handle_t *handle, uint8_t *status)
{
    if (handle == NULL || status == NULL || !handle->initialized) { return (int)IOT_INVALID_PARAM; }
    return hmc5883l_read_reg(handle, HMC5883L_REG_STATUS, status, 1U);
}

int hmc5883l_set_gain(hmc5883l_handle_t *handle, hmc5883l_gain_t gain)
{
    uint8_t check = 0U;
    uint8_t value;
    if (handle == NULL || !handle->initialized || (unsigned)gain >= (unsigned)HMC5883L_GAIN_COUNT) {
        return (int)IOT_INVALID_PARAM;
    }
    value = (uint8_t)((uint8_t)gain << HMC5883L_CFGB_GN_SHIFT);
    if (hmc5883l_write(handle, HMC5883L_REG_CFG_B, value) != DRIVER_OK) { return (int)IOT_IO_ERROR; }
    if (hmc5883l_read_reg(handle, HMC5883L_REG_CFG_B, &check, 1U) != DRIVER_OK ||
        (check & HMC5883L_CFGB_GN_MASK) != value) {
        LOG_E("hmc5883l", "cfg_b readback 0x%02X mismatch", check);
        return (int)IOT_HW_ERROR;
    }
    handle->gain = gain;
    handle->ut_per_lsb = HMC5883L_UT_PER_GAUSS / s_lsb_per_gauss[gain];
    return DRIVER_OK;
}

int hmc5883l_init(hmc5883l_handle_t *handle, iot_i2c_t *i2c, uint16_t address,
                  hmc5883l_gain_t gain, hmc5883l_average_t average,
                  hmc5883l_data_rate_t data_rate)
{
    uint8_t id[HMC5883L_ID_LEN];
    uint8_t check = 0U;
    uint8_t cfga;
    uint8_t cfgb;
    if (handle == NULL || i2c == NULL ||
        (unsigned)gain >= (unsigned)HMC5883L_GAIN_COUNT ||
        (unsigned)average >= (unsigned)HMC5883L_AVG_COUNT ||
        (unsigned)data_rate >= (unsigned)HMC5883L_DR_COUNT) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->i2c = (driver_i2c_t){ .handle = i2c, .timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT,
                                  .address = (address != 0U) ? address : HMC5883L_DEFAULT_ADDRESS };
    handle->average = average;
    handle->data_rate = data_rate;
    handle->gain = gain;
    handle->ut_per_lsb = HMC5883L_UT_PER_GAUSS / s_lsb_per_gauss[gain];
    handle->initialized = false;

    /* 身份校验：ID_A/ID_B/ID_C = 'H' '4' '3' */
    if (hmc5883l_read_reg(handle, HMC5883L_REG_ID_A, id, sizeof(id)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    if (id[0] != HMC5883L_ID_A_VALUE || id[1] != HMC5883L_ID_B_VALUE ||
        id[2] != HMC5883L_ID_C_VALUE) {
        LOG_E("hmc5883l", "id mismatch %02X %02X %02X", id[0], id[1], id[2]);
        return (int)IOT_NOT_FOUND;
    }

    /* CFG_A：平均次数 + 输出率 + 正常测量；CFG_B：增益；MODE：连续测量 */
    cfga = (uint8_t)(((uint8_t)average << HMC5883L_CFGA_MA_SHIFT) |
                     ((uint8_t)data_rate << HMC5883L_CFGA_DO_SHIFT));
    cfgb = (uint8_t)((uint8_t)gain << HMC5883L_CFGB_GN_SHIFT);
    if (hmc5883l_write(handle, HMC5883L_REG_CFG_A, cfga) != DRIVER_OK ||
        hmc5883l_write(handle, HMC5883L_REG_CFG_B, cfgb) != DRIVER_OK ||
        hmc5883l_write(handle, HMC5883L_REG_MODE, HMC5883L_MODE_CONTINUOUS) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(HMC5883L_START_DELAY_MS);

    /* 写通路自检：回读 CFG_A / CFG_B，并确认 STATUS.LOCK 未置位 */
    if (hmc5883l_read_reg(handle, HMC5883L_REG_CFG_A, &check, 1U) != DRIVER_OK ||
        (check & (HMC5883L_CFGA_MA_MASK | HMC5883L_CFGA_DO_MASK)) != cfga) {
        LOG_E("hmc5883l", "cfg_a readback 0x%02X mismatch", check);
        return (int)IOT_HW_ERROR;
    }
    if (hmc5883l_read_reg(handle, HMC5883L_REG_CFG_B, &check, 1U) != DRIVER_OK ||
        (check & HMC5883L_CFGB_GN_MASK) != cfgb) {
        LOG_E("hmc5883l", "cfg_b readback 0x%02X mismatch", check);
        return (int)IOT_HW_ERROR;
    }
    if (hmc5883l_read_reg(handle, HMC5883L_REG_STATUS, &check, 1U) != DRIVER_OK ||
        (check & HMC5883L_STATUS_LOCK) != 0U) {
        LOG_E("hmc5883l", "status lock, 0x%02X", check);
        return (int)IOT_HW_ERROR;
    }

    handle->initialized = true;
    LOG_I("hmc5883l", "init ok at 0x%02X, gain=%d", (int)handle->i2c.address, (int)gain);
    return DRIVER_OK;
}

int hmc5883l_deinit(hmc5883l_handle_t *handle)
{
    if (handle == NULL) { return (int)IOT_INVALID_PARAM; }
    if (handle->initialized) {
        (void)hmc5883l_write(handle, HMC5883L_REG_MODE, HMC5883L_MODE_IDLE);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int hmc5883l_read_raw(hmc5883l_handle_t *handle, int16_t xyz[3])
{
    uint8_t d[HMC5883L_DATA_BURST_LEN];
    uint8_t status = 0U;
    uint32_t start;
    if (handle == NULL || xyz == NULL || !handle->initialized) { return (int)IOT_INVALID_PARAM; }

    start = driver_tick_ms();
    do {
        if (hmc5883l_read_reg(handle, HMC5883L_REG_STATUS, &status, 1U) != DRIVER_OK) {
            return (int)IOT_IO_ERROR;
        }
        if ((status & HMC5883L_STATUS_RDY) != 0U) { break; }
        driver_delay_ms(1U);
    } while ((uint32_t)(driver_tick_ms() - start) <= HMC5883L_DATA_TIMEOUT_MS);
    if ((status & HMC5883L_STATUS_RDY) == 0U) { return (int)IOT_TIMEOUT; }

    if (hmc5883l_read_reg(handle, HMC5883L_REG_DATA_X_MSB, d, sizeof(d)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    /* 字节序为 X, Z, Y：0x03/0x04 = X，0x05/0x06 = Z，0x07/0x08 = Y */
    xyz[0] = driver_be16(&d[0]);
    xyz[1] = driver_be16(&d[4]);
    xyz[2] = driver_be16(&d[2]);
    return DRIVER_OK;
}

int hmc5883l_read(hmc5883l_handle_t *handle, hmc5883l_data_t *data)
{
    int16_t xyz[3];
    uint8_t status = 0U;
    int ret;
    if (handle == NULL || data == NULL) { return (int)IOT_INVALID_PARAM; }
    ret = hmc5883l_read_raw(handle, xyz);
    if (ret != DRIVER_OK) { return ret; }
    if (hmc5883l_get_status(handle, &status) != DRIVER_OK) { return (int)IOT_IO_ERROR; }

    data->mag_ut.x = (float)xyz[0] * handle->ut_per_lsb;
    data->mag_ut.y = (float)xyz[1] * handle->ut_per_lsb;
    data->mag_ut.z = (float)xyz[2] * handle->ut_per_lsb;
    data->heading_deg = atan2f(data->mag_ut.y, data->mag_ut.x) * 57.2957795f;
    if (data->heading_deg < 0.0f) { data->heading_deg += 360.0f; }
    data->data_ready = ((status & HMC5883L_STATUS_RDY) != 0U);
    data->locked = ((status & HMC5883L_STATUS_LOCK) != 0U);
    return DRIVER_OK;
}
