#include "mpu6050.h"
#include "mpu6050_register.h"
#include "log.h"

static const float s_accel_lsb_per_g[MPU6050_ACCEL_RANGE_COUNT] = {
    MPU6050_ACCEL_LSB_PER_G_2G,
    MPU6050_ACCEL_LSB_PER_G_4G,
    MPU6050_ACCEL_LSB_PER_G_8G,
    MPU6050_ACCEL_LSB_PER_G_16G
};

static const float s_gyro_lsb_per_dps[MPU6050_GYRO_RANGE_COUNT] = {
    MPU6050_GYRO_LSB_PER_DPS_250,
    MPU6050_GYRO_LSB_PER_DPS_500,
    MPU6050_GYRO_LSB_PER_DPS_1000,
    MPU6050_GYRO_LSB_PER_DPS_2000
};

static int mpu6050_write_reg(mpu6050_handle_t *handle, uint8_t reg, uint8_t value)
{
    if (driver_i2c_write_reg(&handle->i2c, reg, &value, 1U) != DRIVER_OK) {
        LOG_E("mpu6050", "write reg 0x%02X failed", reg);
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

static int mpu6050_read_reg(mpu6050_handle_t *handle, uint8_t reg, uint8_t *value)
{
    if (driver_i2c_read_reg(&handle->i2c, reg, value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

/* 写入量程并回读校验，作为写通路自检 */
static int mpu6050_apply_range(mpu6050_handle_t *handle)
{
    uint8_t gyro_cfg = (uint8_t)((uint8_t)handle->gyro_range << MPU6050_GYRO_FS_SHIFT);
    uint8_t accel_cfg = (uint8_t)((uint8_t)handle->accel_range << MPU6050_ACCEL_FS_SHIFT);
    uint8_t check = 0U;

    if (mpu6050_write_reg(handle, MPU6050_REG_GYRO_CONFIG, gyro_cfg) != DRIVER_OK ||
        mpu6050_write_reg(handle, MPU6050_REG_ACCEL_CONFIG, accel_cfg) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (mpu6050_read_reg(handle, MPU6050_REG_GYRO_CONFIG, &check) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if ((check & MPU6050_GYRO_FS_MASK) != (gyro_cfg & MPU6050_GYRO_FS_MASK)) {
        LOG_E("mpu6050", "gyro range readback 0x%02X mismatch", check);
        return (int)HAL_HW_ERROR;
    }
    if (mpu6050_read_reg(handle, MPU6050_REG_ACCEL_CONFIG, &check) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if ((check & MPU6050_ACCEL_FS_MASK) != (accel_cfg & MPU6050_ACCEL_FS_MASK)) {
        LOG_E("mpu6050", "accel range readback 0x%02X mismatch", check);
        return (int)HAL_HW_ERROR;
    }

    handle->accel_lsb_per_g = s_accel_lsb_per_g[handle->accel_range];
    handle->gyro_lsb_per_dps = s_gyro_lsb_per_dps[handle->gyro_range];
    return DRIVER_OK;
}

int mpu6050_set_dlpf(mpu6050_handle_t *handle, mpu6050_dlpf_e dlpf)
{
    if (handle == NULL || handle->i2c.handle == NULL ||
        (unsigned)dlpf > (unsigned)MPU6050_DLPF_BW_5HZ) {
        return (int)HAL_INVALID_PARAM;
    }
    if (mpu6050_write_reg(handle, MPU6050_REG_CONFIG, (uint8_t)dlpf) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    handle->dlpf = dlpf;
    return DRIVER_OK;
}

int mpu6050_set_sample_rate(mpu6050_handle_t *handle, uint16_t rate_hz)
{
    uint32_t div;

    if (handle == NULL || handle->i2c.handle == NULL ||
        rate_hz < 4U || rate_hz > MPU6050_SAMPLE_RATE_BASE_HZ) {
        return (int)HAL_INVALID_PARAM;
    }

    /* DLPF 使能时陀螺仪采样基准为 1kHz：rate = 1kHz / (1 + div) */
    div = MPU6050_SAMPLE_RATE_BASE_HZ / rate_hz;
    if (div == 0U) {
        div = 1U;
    }
    div -= 1U;
    if (div > MPU6050_SMPLRT_DIV_MAX) {
        div = MPU6050_SMPLRT_DIV_MAX;
    }
    return mpu6050_write_reg(handle, MPU6050_REG_SMPLRT_DIV, (uint8_t)div);
}

int mpu6050_init(mpu6050_handle_t *handle, hal_i2c_t *i2c, uint16_t address,
                 mpu6050_accel_range_e accel_range, mpu6050_gyro_range_e gyro_range)
{
    uint8_t id = 0U;

    if (handle == NULL || i2c == NULL ||
        (unsigned)accel_range >= (unsigned)MPU6050_ACCEL_RANGE_COUNT ||
        (unsigned)gyro_range >= (unsigned)MPU6050_GYRO_RANGE_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : MPU6050_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->accel_range = accel_range;
    handle->gyro_range = gyro_range;
    handle->accel_lsb_per_g = s_accel_lsb_per_g[accel_range];
    handle->gyro_lsb_per_dps = s_gyro_lsb_per_dps[gyro_range];
    handle->dlpf = MPU6050_DLPF_BW_44HZ;
    handle->initialized = false;

    /* 软复位，恢复默认配置 */
    if (mpu6050_write_reg(handle, MPU6050_REG_PWR_MGMT_1, MPU6050_PWR1_DEVICE_RESET) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(MPU6050_RESET_DELAY_MS);

    /* WHO_AM_I 在位校验 */
    if (mpu6050_read_reg(handle, MPU6050_REG_WHO_AM_I, &id) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if ((id & MPU6050_WHO_AM_I_MASK) != MPU6050_WHO_AM_I_VALUE) {
        LOG_E("mpu6050", "who_am_i mismatch 0x%02X", id);
        return (int)HAL_NOT_FOUND;
    }

    /* 唤醒，时钟源选 X 轴陀螺 PLL（比内部 RC 稳定） */
    if (mpu6050_write_reg(handle, MPU6050_REG_PWR_MGMT_1, MPU6050_PWR1_CLKSEL_PLL_X) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(MPU6050_WAKE_DELAY_MS);

    if (mpu6050_set_dlpf(handle, MPU6050_DLPF_BW_44HZ) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (mpu6050_apply_range(handle) != DRIVER_OK) {
        return (int)HAL_HW_ERROR;
    }
    if (mpu6050_set_sample_rate(handle, 100U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    /* 使能数据就绪中断，read 前可先 poll INT_STATUS */
    if (mpu6050_write_reg(handle, MPU6050_REG_INT_ENABLE, MPU6050_INT_DATA_RDY) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("mpu6050", "init ok addr=0x%02X accel_range=%d gyro_range=%d",
          (int)handle->i2c.address, (int)accel_range, (int)gyro_range);
    return DRIVER_OK;
}

int mpu6050_deinit(mpu6050_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)mpu6050_write_reg(handle, MPU6050_REG_PWR_MGMT_1, MPU6050_PWR1_SLEEP);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int mpu6050_get_status(mpu6050_handle_t *handle, uint8_t *int_status)
{
    if (handle == NULL || int_status == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return mpu6050_read_reg(handle, MPU6050_REG_INT_STATUS, int_status);
}

int mpu6050_read_raw(mpu6050_handle_t *handle, int16_t accel[3], int16_t gyro[3],
                     int16_t *temperature_raw)
{
    uint8_t d[MPU6050_DATA_BURST_LEN];
    int16_t temp;

    if (handle == NULL || accel == NULL || gyro == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (driver_i2c_read_reg(&handle->i2c, MPU6050_REG_ACCEL_XOUT_H, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 大端：高字节在前，0x3B 起依次 ax, ay, az, temp, gx, gy, gz */
    accel[0] = driver_be16(&d[0]);
    accel[1] = driver_be16(&d[2]);
    accel[2] = driver_be16(&d[4]);
    temp = driver_be16(&d[6]);
    gyro[0] = driver_be16(&d[8]);
    gyro[1] = driver_be16(&d[10]);
    gyro[2] = driver_be16(&d[12]);

    /* 全 0 / 全 1 说明总线读回无效数据（器件掉电或悬空） */
    if ((accel[0] == 0 && accel[1] == 0 && accel[2] == 0 &&
         gyro[0] == 0 && gyro[1] == 0 && gyro[2] == 0) ||
        (accel[0] == -1 && accel[1] == -1 && accel[2] == -1)) {
        LOG_W("mpu6050", "invalid data pattern");
        return (int)HAL_HW_ERROR;
    }

    if (temperature_raw != NULL) {
        *temperature_raw = temp;
    }
    return DRIVER_OK;
}

int mpu6050_read(mpu6050_handle_t *handle, mpu6050_data_t *data)
{
    int16_t accel[3];
    int16_t gyro[3];
    int16_t temp = 0;
    int ret;

    if (handle == NULL || data == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = mpu6050_read_raw(handle, accel, gyro, &temp);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->accel_g.x = (float)accel[0] / handle->accel_lsb_per_g;
    data->accel_g.y = (float)accel[1] / handle->accel_lsb_per_g;
    data->accel_g.z = (float)accel[2] / handle->accel_lsb_per_g;
    data->gyro_dps.x = (float)gyro[0] / handle->gyro_lsb_per_dps;
    data->gyro_dps.y = (float)gyro[1] / handle->gyro_lsb_per_dps;
    data->gyro_dps.z = (float)gyro[2] / handle->gyro_lsb_per_dps;
    data->temperature_c = (float)temp / MPU6050_TEMP_LSB_PER_DEGC + MPU6050_TEMP_OFFSET_DEGC;
    return DRIVER_OK;
}
