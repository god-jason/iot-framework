#include "bmi270.h"
#include "bmi270_register.h"
#include "log.h"

#define BMI270_TAG "bmi270"

/* 量程 -> 灵敏度（LSB 每 g） */
static const float s_accel_lsb_per_g[BMI270_ACCEL_RANGE_COUNT] = {
    BMI270_ACC_LSB_PER_G_2G, BMI270_ACC_LSB_PER_G_4G,
    BMI270_ACC_LSB_PER_G_8G, BMI270_ACC_LSB_PER_G_16G
};

/* 量程 -> 灵敏度（LSB 每 °/s） */
static const float s_gyro_lsb_per_dps[BMI270_GYRO_RANGE_COUNT] = {
    BMI270_GYR_LSB_PER_DPS_2000, BMI270_GYR_LSB_PER_DPS_1000, BMI270_GYR_LSB_PER_DPS_500,
    BMI270_GYR_LSB_PER_DPS_250, BMI270_GYR_LSB_PER_DPS_125
};

/* 量程 -> 寄存器编码 */
static const uint8_t s_accel_range_code[BMI270_ACCEL_RANGE_COUNT] = {
    BMI270_ACC_RANGE_CODE_2G, BMI270_ACC_RANGE_CODE_4G,
    BMI270_ACC_RANGE_CODE_8G, BMI270_ACC_RANGE_CODE_16G
};

static const uint8_t s_gyro_range_code[BMI270_GYRO_RANGE_COUNT] = {
    BMI270_GYR_RANGE_CODE_2000DPS, BMI270_GYR_RANGE_CODE_1000DPS, BMI270_GYR_RANGE_CODE_500DPS,
    BMI270_GYR_RANGE_CODE_250DPS, BMI270_GYR_RANGE_CODE_125DPS
};

/* 输出率 -> 寄存器编码（加速度计与陀螺仪 ODR 字段编码一致） */
static const uint8_t s_odr_code[BMI270_ODR_COUNT] = {
    BMI270_ODR_CODE_25HZ, BMI270_ODR_CODE_50HZ, BMI270_ODR_CODE_100HZ, BMI270_ODR_CODE_200HZ,
    BMI270_ODR_CODE_400HZ, BMI270_ODR_CODE_800HZ, BMI270_ODR_CODE_1600HZ
};

static int bmi270_write_reg(bmi270_handle_t *handle, uint8_t reg, uint8_t value)
{
    return driver_i2c_write_reg(&handle->i2c, reg, &value, 1U);
}

static int bmi270_write_conf(bmi270_handle_t *handle)
{
    uint8_t acc_conf = BMI270_ACC_CONF_VALUE(BMI270_ACC_BWP_NORMAL, s_odr_code[handle->odr]);
    uint8_t gyr_conf = BMI270_GYR_CONF_VALUE(BMI270_GYR_BWP_NORMAL, s_odr_code[handle->odr]);

    if (bmi270_write_reg(handle, BMI270_REG_ACC_CONF, acc_conf) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (bmi270_write_reg(handle, BMI270_REG_ACC_RANGE, s_accel_range_code[handle->accel_range])
        != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (bmi270_write_reg(handle, BMI270_REG_GYR_CONF, gyr_conf) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (bmi270_write_reg(handle, BMI270_REG_GYR_RANGE, s_gyro_range_code[handle->gyro_range])
        != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

int bmi270_init(bmi270_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t id = 0U;
    uint8_t status = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : BMI270_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->accel_range = BMI270_ACCEL_RANGE_2G;
    handle->gyro_range = BMI270_GYRO_RANGE_500DPS;
    handle->odr = BMI270_ODR_100HZ;
    handle->accel_lsb_per_g = s_accel_lsb_per_g[BMI270_ACCEL_RANGE_2G];
    handle->gyro_lsb_per_dps = s_gyro_lsb_per_dps[BMI270_GYRO_RANGE_500DPS];
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E(BMI270_TAG, "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }
    if (driver_i2c_read_reg(&handle->i2c, BMI270_REG_CHIP_ID, &id, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (id != BMI270_CHIP_ID_VALUE) {
        LOG_E(BMI270_TAG, "chip id mismatch: 0x%02X", id);
        return (int)HAL_NOT_FOUND;
    }

    /* 关闭高级省电模式，进入性能模式；写后需等待 450us 才能继续配置 */
    if (bmi270_write_reg(handle, BMI270_REG_PWR_CONF, BMI270_PWR_CONF_PERF) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_us(BMI270_POWER_UP_DELAY_US);

    if (bmi270_write_conf(handle) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 使能加速度计与陀螺仪 */
    if (bmi270_write_reg(handle, BMI270_REG_PWR_CTRL, BMI270_PWR_CTRL_ACC_GYR) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(BMI270_START_UP_DELAY_MS);

    /* 状态非 0x01 说明配置固件未加载，此时数据寄存器可能无效，仅告警不失败 */
    if (driver_i2c_read_reg(&handle->i2c, BMI270_REG_INTERNAL_STATUS, &status, 1U) == DRIVER_OK) {
        if ((status & BMI270_INTERNAL_STATUS_MSG_MASK) != BMI270_INTERNAL_STATUS_INIT_OK) {
            LOG_W(BMI270_TAG, "config firmware not loaded (status 0x%02X)", status);
        }
    }

    handle->initialized = true;
    LOG_I(BMI270_TAG, "init ok at 0x%02X, acc=%d, gyro=%d",
          handle->i2c.address, (int)handle->accel_range, (int)handle->gyro_range);
    return DRIVER_OK;
}

int bmi270_deinit(bmi270_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized && handle->i2c.handle != NULL) {
        (void)bmi270_write_reg(handle, BMI270_REG_PWR_CTRL, 0x00U);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int bmi270_read_raw(bmi270_handle_t *handle, int16_t accel[3], int16_t gyro[3])
{
    uint8_t d[12];

    if (handle == NULL || accel == NULL || gyro == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (driver_i2c_read_reg(&handle->i2c, BMI270_REG_ACC_X_L, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    accel[0] = driver_le16(&d[0]);
    accel[1] = driver_le16(&d[2]);
    accel[2] = driver_le16(&d[4]);
    gyro[0] = driver_le16(&d[6]);
    gyro[1] = driver_le16(&d[8]);
    gyro[2] = driver_le16(&d[10]);
    return DRIVER_OK;
}

int bmi270_read(bmi270_handle_t *handle, bmi270_data_t *data)
{
    int16_t accel[3];
    int16_t gyro[3];
    int ret;

    if (handle == NULL || data == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = bmi270_read_raw(handle, accel, gyro);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->accel_g.x = (float)accel[0] / handle->accel_lsb_per_g;
    data->accel_g.y = (float)accel[1] / handle->accel_lsb_per_g;
    data->accel_g.z = (float)accel[2] / handle->accel_lsb_per_g;

    data->gyro_dps.x = (float)gyro[0] / handle->gyro_lsb_per_dps;
    data->gyro_dps.y = (float)gyro[1] / handle->gyro_lsb_per_dps;
    data->gyro_dps.z = (float)gyro[2] / handle->gyro_lsb_per_dps;
    return DRIVER_OK;
}

int bmi270_set_accel_range(bmi270_handle_t *handle, bmi270_accel_range_e range)
{
    if (handle == NULL || !handle->initialized ||
        (unsigned)range >= (unsigned)BMI270_ACCEL_RANGE_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }
    if (bmi270_write_reg(handle, BMI270_REG_ACC_RANGE, s_accel_range_code[range]) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->accel_range = range;
    handle->accel_lsb_per_g = s_accel_lsb_per_g[range];
    return DRIVER_OK;
}

int bmi270_set_gyro_range(bmi270_handle_t *handle, bmi270_gyro_range_e range)
{
    if (handle == NULL || !handle->initialized ||
        (unsigned)range >= (unsigned)BMI270_GYRO_RANGE_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }
    if (bmi270_write_reg(handle, BMI270_REG_GYR_RANGE, s_gyro_range_code[range]) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->gyro_range = range;
    handle->gyro_lsb_per_dps = s_gyro_lsb_per_dps[range];
    return DRIVER_OK;
}

int bmi270_set_output_rate(bmi270_handle_t *handle, bmi270_odr_e odr)
{
    uint8_t acc_conf;
    uint8_t gyr_conf;

    if (handle == NULL || !handle->initialized || (unsigned)odr >= (unsigned)BMI270_ODR_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }

    acc_conf = BMI270_ACC_CONF_VALUE(BMI270_ACC_BWP_NORMAL, s_odr_code[odr]);
    gyr_conf = BMI270_GYR_CONF_VALUE(BMI270_GYR_BWP_NORMAL, s_odr_code[odr]);

    if (bmi270_write_reg(handle, BMI270_REG_ACC_CONF, acc_conf) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (bmi270_write_reg(handle, BMI270_REG_GYR_CONF, gyr_conf) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->odr = odr;
    return DRIVER_OK;
}

int bmi270_get_internal_status(bmi270_handle_t *handle, uint8_t *msg)
{
    uint8_t status = 0U;

    if (handle == NULL || msg == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (driver_i2c_read_reg(&handle->i2c, BMI270_REG_INTERNAL_STATUS, &status, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    *msg = (uint8_t)(status & BMI270_INTERNAL_STATUS_MSG_MASK);
    return DRIVER_OK;
}
