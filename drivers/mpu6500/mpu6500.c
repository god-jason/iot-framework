#include "mpu6500.h"
#include "mpu6500_register.h"
#include "log.h"

static const float s_accel_lsb_per_g[MPU6500_ACCEL_RANGE_COUNT] = {
    MPU6500_ACCEL_LSB_PER_G_2G,
    MPU6500_ACCEL_LSB_PER_G_4G,
    MPU6500_ACCEL_LSB_PER_G_8G,
    MPU6500_ACCEL_LSB_PER_G_16G
};

static const float s_gyro_lsb_per_dps[MPU6500_GYRO_RANGE_COUNT] = {
    MPU6500_GYRO_LSB_PER_DPS_250,
    MPU6500_GYRO_LSB_PER_DPS_500,
    MPU6500_GYRO_LSB_PER_DPS_1000,
    MPU6500_GYRO_LSB_PER_DPS_2000
};

static int mpu6500_write(mpu6500_handle_t *handle, uint8_t reg, uint8_t value)
{
    uint8_t tx[2];
    int ret;

    if (!handle->use_spi) {
        return (driver_i2c_write_reg(&handle->i2c, reg, &value, 1U) == DRIVER_OK)
                   ? DRIVER_OK : (int)IOT_IO_ERROR;
    }

    tx[0] = (uint8_t)(reg & 0x7FU);
    tx[1] = value;
    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, NULL, sizeof(tx));
    driver_spi_deselect(&handle->spi);
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)IOT_IO_ERROR;
}

static int mpu6500_bus_read(mpu6500_handle_t *handle, uint8_t reg, uint8_t *buf, size_t len)
{
    uint8_t tx[MPU6500_BURST_MAX + 1U];
    uint8_t rx[MPU6500_BURST_MAX + 1U];
    size_t i;
    int ret;

    if (len == 0U || len > MPU6500_BURST_MAX) {
        return (int)IOT_INVALID_PARAM;
    }
    if (!handle->use_spi) {
        return (driver_i2c_read_reg(&handle->i2c, reg, buf, len) == DRIVER_OK)
                   ? DRIVER_OK : (int)IOT_IO_ERROR;
    }

    /* bit7 读标志，bit6 地址自增（多字节突发） */
    tx[0] = (uint8_t)(reg | 0x80U | ((len > 1U) ? 0x40U : 0x00U));
    for (i = 1U; i <= len; i++) {
        tx[i] = 0U;
    }
    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, rx, len + 1U);
    driver_spi_deselect(&handle->spi);
    if (ret != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    for (i = 0U; i < len; i++) {
        buf[i] = rx[i + 1U];
    }
    return DRIVER_OK;
}

int mpu6500_set_sample_rate(mpu6500_handle_t *handle, uint16_t rate_hz)
{
    uint32_t div;

    if (handle == NULL || (handle->i2c.handle == NULL && handle->spi.handle == NULL) ||
        rate_hz < 4U || rate_hz > MPU6500_SAMPLE_RATE_BASE_HZ) {
        return (int)IOT_INVALID_PARAM;
    }

    /* DLPF 使能时采样基准 1kHz：rate = 1kHz / (1 + div) */
    div = MPU6500_SAMPLE_RATE_BASE_HZ / rate_hz;
    if (div == 0U) {
        div = 1U;
    }
    div -= 1U;
    if (div > MPU6500_SMPLRT_DIV_MAX) {
        div = MPU6500_SMPLRT_DIV_MAX;
    }
    return mpu6500_write(handle, MPU6500_REG_SMPLRT_DIV, (uint8_t)div);
}

int mpu6500_init(mpu6500_handle_t *handle, iot_i2c_t *i2c,
                 iot_spi_t *spi, iot_pin_t cs, uint16_t address,
                 mpu6500_accel_range_t accel_range, mpu6500_gyro_range_t gyro_range)
{
    uint8_t id = 0U;
    uint8_t value;
    uint8_t check = 0U;

    if (handle == NULL || (i2c == NULL && spi == NULL) ||
        (unsigned)accel_range >= (unsigned)MPU6500_ACCEL_RANGE_COUNT ||
        (unsigned)gyro_range >= (unsigned)MPU6500_GYRO_RANGE_COUNT) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->use_spi = (spi != NULL);
    handle->i2c = (driver_i2c_t){ .handle = i2c, .timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT,
                                  .address = (address != 0U) ? address : MPU6500_DEFAULT_ADDRESS };
    handle->spi = (driver_spi_t){ .handle = spi, .cs = cs, .timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT };
    handle->accel_range = accel_range; handle->gyro_range = gyro_range;
    handle->accel_lsb_per_g = s_accel_lsb_per_g[accel_range];
    handle->gyro_lsb_per_dps = s_gyro_lsb_per_dps[gyro_range];
    handle->initialized = false;

    /* 软复位 */
    if (mpu6500_write(handle, MPU6500_REG_PWR_MGMT_1, MPU6500_PWR1_DEVICE_RESET) != DRIVER_OK) { return (int)IOT_IO_ERROR; }
    driver_delay_ms(MPU6500_RESET_DELAY_MS);

    /* WHO_AM_I 在位校验 */
    if (mpu6500_bus_read(handle, MPU6500_REG_WHO_AM_I, &id, 1U) != DRIVER_OK) { return (int)IOT_IO_ERROR; }
    if (id != MPU6500_WHO_AM_I_VALUE) {
        LOG_E("mpu6500", "who_am_i mismatch 0x%02X", id);
        return (int)IOT_NOT_FOUND;
    }

    /* SPI 模式需关闭芯片 I2C 从口，避免总线争用 */
    if (handle->use_spi &&
        mpu6500_write(handle, MPU6500_REG_USER_CTRL, MPU6500_USER_I2C_IF_DIS) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    /* 唤醒，时钟源选 X 轴陀螺 PLL */
    if (mpu6500_write(handle, MPU6500_REG_PWR_MGMT_1, MPU6500_PWR1_CLKSEL_PLL_X) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(MPU6500_WAKE_DELAY_MS);

    /* 陀螺 / 加速度 DLPF ≈ 41Hz 带宽 */
    if (mpu6500_write(handle, MPU6500_REG_CONFIG, MPU6500_DLPF_41HZ) != DRIVER_OK ||
        mpu6500_write(handle, MPU6500_REG_ACCEL_CONFIG2, MPU6500_DLPF_41HZ) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    /* 量程设置 */
    value = (uint8_t)((uint8_t)gyro_range << MPU6500_GYRO_FS_SHIFT);
    if (mpu6500_write(handle, MPU6500_REG_GYRO_CONFIG, value) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    value = (uint8_t)((uint8_t)accel_range << MPU6500_ACCEL_FS_SHIFT);
    if (mpu6500_write(handle, MPU6500_REG_ACCEL_CONFIG, value) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    /* 写通路自检：回读量程位 */
    if (mpu6500_bus_read(handle, MPU6500_REG_GYRO_CONFIG, &check, 1U) != DRIVER_OK ||
        (check & MPU6500_GYRO_FS_MASK) != (uint8_t)((uint8_t)gyro_range << MPU6500_GYRO_FS_SHIFT)) {
        LOG_E("mpu6500", "gyro range readback failed");
        return (int)IOT_HW_ERROR;
    }
    if (mpu6500_bus_read(handle, MPU6500_REG_ACCEL_CONFIG, &check, 1U) != DRIVER_OK ||
        (check & MPU6500_ACCEL_FS_MASK) != (uint8_t)((uint8_t)accel_range << MPU6500_ACCEL_FS_SHIFT)) {
        LOG_E("mpu6500", "accel range readback failed");
        return (int)IOT_HW_ERROR;
    }

    if (mpu6500_set_sample_rate(handle, 100U) != DRIVER_OK ||
        mpu6500_write(handle, MPU6500_REG_INT_ENABLE, MPU6500_INT_DATA_RDY) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("mpu6500", "init ok bus=%s addr=0x%02X", handle->use_spi ? "spi" : "i2c",
          (int)handle->i2c.address);
    return DRIVER_OK;
}

int mpu6500_deinit(mpu6500_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)mpu6500_write(handle, MPU6500_REG_PWR_MGMT_1, MPU6500_PWR1_SLEEP);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    handle->spi.handle = NULL;
    return DRIVER_OK;
}

int mpu6500_get_status(mpu6500_handle_t *handle, uint8_t *int_status)
{
    if (handle == NULL || int_status == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return mpu6500_bus_read(handle, MPU6500_REG_INT_STATUS, int_status, 1U);
}

int mpu6500_read_raw(mpu6500_handle_t *handle, int16_t accel[3], int16_t gyro[3],
                     int16_t *temperature_raw)
{
    uint8_t d[MPU6500_DATA_BURST_LEN];

    if (handle == NULL || accel == NULL || gyro == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (mpu6500_bus_read(handle, MPU6500_REG_ACCEL_XOUT_H, d, sizeof(d)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    accel[0] = driver_be16(&d[0]);
    accel[1] = driver_be16(&d[2]);
    accel[2] = driver_be16(&d[4]);
    gyro[0] = driver_be16(&d[8]);
    gyro[1] = driver_be16(&d[10]);
    gyro[2] = driver_be16(&d[12]);

    if (accel[0] == 0 && accel[1] == 0 && accel[2] == 0 &&
        gyro[0] == 0 && gyro[1] == 0 && gyro[2] == 0) {
        LOG_W("mpu6500", "all-zero data (device not responding)");
        return (int)IOT_HW_ERROR;
    }
    if (temperature_raw != NULL) {
        *temperature_raw = driver_be16(&d[6]);
    }
    return DRIVER_OK;
}

int mpu6500_read(mpu6500_handle_t *handle, mpu6500_data_t *data)
{
    int16_t accel[3];
    int16_t gyro[3];
    int16_t temp = 0;
    int ret;

    if (handle == NULL || data == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = mpu6500_read_raw(handle, accel, gyro, &temp);
    if (ret != DRIVER_OK) {
        return ret;
    }

    data->accel_g.x = (float)accel[0] / handle->accel_lsb_per_g;
    data->accel_g.y = (float)accel[1] / handle->accel_lsb_per_g;
    data->accel_g.z = (float)accel[2] / handle->accel_lsb_per_g;
    data->gyro_dps.x = (float)gyro[0] / handle->gyro_lsb_per_dps;
    data->gyro_dps.y = (float)gyro[1] / handle->gyro_lsb_per_dps;
    data->gyro_dps.z = (float)gyro[2] / handle->gyro_lsb_per_dps;
    data->temperature_c = (float)temp / MPU6500_TEMP_LSB_PER_DEGC + MPU6500_TEMP_OFFSET_DEGC;
    return DRIVER_OK;
}
