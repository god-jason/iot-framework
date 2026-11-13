#include "mpu9250.h"
#include "mpu9250_register.h"
#include "ros.h"
#include "common.h"
#include <math.h>

static const float s_gyro_lsb_per_dps[MPU9250_GYRO_RANGE_COUNT] = { 131.0f, 65.5f, 32.8f, 16.4f };
static const float s_accel_lsb_per_g[MPU9250_ACCEL_RANGE_COUNT] = { 16384.0f, 8192.0f, 4096.0f, 2048.0f };

static int mpu9250_write(mpu9250_handle_t *handle, uint8_t reg, uint8_t value)
{
    uint8_t tx[2];
    int ret;
    if (!handle->use_spi) {
        return (hal_i2c_dev_write_reg(&handle->i2c, reg, &value, 1U) == HAL_OK) ? HAL_OK : (int)HAL_IO_ERROR;
    }
    tx[0] = (uint8_t)(reg & 0x7FU);
    tx[1] = value;
    hal_spi_dev_select(&handle->spi);
    ret = hal_spi_dev_transfer(&handle->spi, tx, NULL, sizeof(tx));
    hal_spi_dev_deselect(&handle->spi);
    return (ret == HAL_OK) ? HAL_OK : (int)HAL_IO_ERROR;
}

static int mpu9250_bus_read(mpu9250_handle_t *handle, uint8_t reg, uint8_t *buf, size_t len)
{
    uint8_t cmd;
    int ret;
    if (len == 0U || len > MPU9250_BURST_MAX) { return (int)HAL_INVALID_PARAM; }
    if (!handle->use_spi) {
        return (hal_i2c_dev_read_reg(&handle->i2c, reg, buf, len) == HAL_OK) ? HAL_OK : (int)HAL_IO_ERROR;
    }
    /* bit7 = 读，bit6 = 地址自增（突发读） */
    cmd = (uint8_t)(reg | 0x80U | ((len > 1U) ? 0x40U : 0x00U));
    hal_spi_dev_select(&handle->spi);
    ret = hal_spi_dev_write(&handle->spi, &cmd, 1U);
    if (ret == HAL_OK) { ret = hal_spi_dev_read(&handle->spi, buf, len); }
    hal_spi_dev_deselect(&handle->spi);
    return (ret == HAL_OK) ? HAL_OK : (int)HAL_IO_ERROR;
}

/* 航向角：未做倾斜补偿，仅适用于近水平安装 */
static float mpu9250_heading_deg(float x, float y)
{
    float deg = atan2f(y, x) * 57.2957795f;
    return (deg < 0.0f) ? (deg + 360.0f) : deg;
}

/* 读 ASA 并进入 16 位 100Hz 连续测量；磁力计缺失时只告警，不算初始化失败 */
static int mpu9250_mag_init(mpu9250_handle_t *handle)
{
    uint8_t wia = 0U, asa[AK8963_ASA_COUNT], value, i;
    handle->mag_present = false;
    if (hal_i2c_dev_read_reg(&handle->mag, AK8963_REG_WIA, &wia, 1U) != HAL_OK || wia != AK8963_WIA_VALUE) {
        LOG_W("mpu9250", "ak8963 not found, wia=0x%02X", wia);
        return HAL_OK;
    }
    value = AK8963_CNTL2_SOFT_RESET;
    (void)hal_i2c_dev_write_reg(&handle->mag, AK8963_REG_CNTL2, &value, 1U);
    hal_delay_ms(1U);
    /* FUSE ROM 模式读三轴出厂灵敏度修正值 */
    value = AK8963_CNTL1_FUSE_ROM;
    if (hal_i2c_dev_write_reg(&handle->mag, AK8963_REG_CNTL1, &value, 1U) != HAL_OK) { return (int)HAL_IO_ERROR; }
    hal_delay_ms(1U);
    if (hal_i2c_dev_read_reg(&handle->mag, AK8963_REG_ASAX, asa, sizeof(asa)) != HAL_OK) { return (int)HAL_IO_ERROR; }
    value = AK8963_CNTL1_POWER_DOWN; (void)hal_i2c_dev_write_reg(&handle->mag, AK8963_REG_CNTL1, &value, 1U);
    hal_delay_ms(1U);
    /* Hadj = H * ((ASA - 128) * 0.5 / 128 + 1)，16 位模式基准 0.15 µT/LSB */
    for (i = 0U; i < AK8963_ASA_COUNT; i++) {
        handle->mag_ut_per_lsb[i] = AK8963_UT_PER_LSB * (1.0f + ((float)asa[i] - 128.0f) * 0.5f / 128.0f);
    }
    value = AK8963_CNTL1_16BIT_100HZ;
    if (hal_i2c_dev_write_reg(&handle->mag, AK8963_REG_CNTL1, &value, 1U) != HAL_OK) { return (int)HAL_IO_ERROR; }
    hal_delay_ms(MPU9250_MAG_START_DELAY_MS);
    handle->mag_present = true;
    LOG_I("mpu9250", "ak8963 ready");
    return HAL_OK;
}

int mpu9250_set_sample_rate(mpu9250_handle_t *handle, uint16_t rate_hz)
{
    uint32_t div;
    if (handle == NULL || (handle->i2c.handle == NULL && handle->spi.handle == NULL) ||
        rate_hz < 4U || rate_hz > MPU9250_SAMPLE_RATE_BASE_HZ) {
        return (int)HAL_INVALID_PARAM;
    }
    /* DLPF 使能时采样基准 1kHz：rate = 1kHz / (1 + div) */
    div = MPU9250_SAMPLE_RATE_BASE_HZ / rate_hz;
    if (div == 0U) { div = 1U; }
    uint8_t value = (uint8_t)((div - 1U < MPU9250_SMPLRT_DIV_MAX)
                              ? (div - 1U) : MPU9250_SMPLRT_DIV_MAX);

    return mpu9250_write(handle, MPU9250_REG_SMPLRT_DIV, value);
}

int mpu9250_init(mpu9250_handle_t *handle, hal_i2c_t *i2c, hal_spi_t *spi,
                 hal_pin_t cs, uint16_t address, mpu9250_accel_range_e accel_range,
                 mpu9250_gyro_range_e gyro_range)
{
    uint8_t id = 0U, check = 0U;
    uint8_t bypass = MPU9250_INT_PIN_BYPASS_EN;
    if (handle == NULL || (i2c == NULL && spi == NULL) ||
        (unsigned)accel_range >= (unsigned)MPU9250_ACCEL_RANGE_COUNT ||
        (unsigned)gyro_range >= (unsigned)MPU9250_GYRO_RANGE_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->use_spi = (spi != NULL);
    handle->i2c = (hal_i2c_dev_t){ .handle = i2c, .timeout_ms = HAL_I2C_TIMEOUT_DEFAULT,
                                  .address = (address != 0U) ? address : MPU9250_DEFAULT_ADDRESS };
    handle->spi = (hal_spi_dev_t){ .handle = spi, .cs = cs, .timeout_ms = HAL_SPI_TIMEOUT_DEFAULT };
    handle->mag = (hal_i2c_dev_t){ .handle = i2c, .address = AK8963_ADDRESS, .timeout_ms = HAL_I2C_TIMEOUT_DEFAULT };
    handle->mag_present = false;
    handle->accel_range = accel_range; handle->gyro_range = gyro_range;
    handle->accel_lsb_per_g = s_accel_lsb_per_g[accel_range];
    handle->gyro_lsb_per_dps = s_gyro_lsb_per_dps[gyro_range];
    handle->initialized = false;
    if (mpu9250_write(handle, MPU9250_REG_PWR_MGMT_1, MPU9250_PWR1_DEVICE_RESET) != HAL_OK) { return (int)HAL_IO_ERROR; }
    hal_delay_ms(MPU9250_RESET_DELAY_MS);
    /* WHO_AM_I 在位校验 */
    if (mpu9250_bus_read(handle, MPU9250_REG_WHO_AM_I, &id, 1U) != HAL_OK) { return (int)HAL_IO_ERROR; }
    if (id != MPU9250_WHO_AM_I_VALUE) {
        LOG_E("mpu9250", "who_am_i 0x%02X mismatch", id);
        return (int)HAL_NOT_FOUND;
    }
    if (handle->use_spi && mpu9250_write(handle, MPU9250_REG_USER_CTRL, MPU9250_USER_I2C_IF_DIS) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (mpu9250_write(handle, MPU9250_REG_PWR_MGMT_1, MPU9250_PWR1_CLKSEL_PLL_X) != HAL_OK) { return (int)HAL_IO_ERROR; }
    hal_delay_ms(MPU9250_WAKE_DELAY_MS);

    /* DLPF ≈41Hz + 量程设置 */
    if (mpu9250_write(handle, MPU9250_REG_CONFIG, MPU9250_DLPF_41HZ) != HAL_OK ||
        mpu9250_write(handle, MPU9250_REG_ACCEL_CONFIG2, MPU9250_DLPF_41HZ) != HAL_OK ||
        mpu9250_write(handle, MPU9250_REG_GYRO_CONFIG,
                      (uint8_t)((uint8_t)gyro_range << MPU9250_GYRO_FS_SHIFT)) != HAL_OK ||
        mpu9250_write(handle, MPU9250_REG_ACCEL_CONFIG,
                      (uint8_t)((uint8_t)accel_range << MPU9250_ACCEL_FS_SHIFT)) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    /* 写通路自检：回读量程位 */
    if (mpu9250_bus_read(handle, MPU9250_REG_GYRO_CONFIG, &check, 1U) != HAL_OK ||
        (check & MPU9250_GYRO_FS_MASK) != (uint8_t)((uint8_t)gyro_range << MPU9250_GYRO_FS_SHIFT) ||
        mpu9250_bus_read(handle, MPU9250_REG_ACCEL_CONFIG, &check, 1U) != HAL_OK ||
        (check & MPU9250_ACCEL_FS_MASK) != (uint8_t)((uint8_t)accel_range << MPU9250_ACCEL_FS_SHIFT)) {
        LOG_E("mpu9250", "range readback failed");
        return (int)HAL_HW_ERROR;
    }
    if (mpu9250_set_sample_rate(handle, 100U) != HAL_OK ||
        mpu9250_write(handle, MPU9250_REG_INT_ENABLE, MPU9250_INT_DATA_RDY) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (!handle->use_spi) {
        /* 旁路使能，使 AK8963 可直接被主机寻址 */
        if (mpu9250_write(handle, MPU9250_REG_INT_PIN_CFG, bypass) != HAL_OK) { return (int)HAL_IO_ERROR; }
        hal_delay_ms(1U);
        if (mpu9250_mag_init(handle) != HAL_OK) { LOG_W("mpu9250", "ak8963 init failed"); }
    }
    handle->initialized = true;
    LOG_I("mpu9250", "init ok bus=%s mag=%d", handle->use_spi ? "spi" : "i2c", handle->mag_present ? 1 : 0);
    return HAL_OK;
}

int mpu9250_deinit(mpu9250_handle_t *handle)
{
    uint8_t value = AK8963_CNTL1_POWER_DOWN;
    if (handle == NULL) { return (int)HAL_INVALID_PARAM; }
    if (handle->initialized) {
        if (handle->mag_present) { (void)hal_i2c_dev_write_reg(&handle->mag, AK8963_REG_CNTL1, &value, 1U); }
        (void)mpu9250_write(handle, MPU9250_REG_PWR_MGMT_1, MPU9250_PWR1_SLEEP);
    }
    handle->initialized = false; handle->mag_present = false;
    handle->i2c.handle = NULL; handle->spi.handle = NULL; handle->mag.handle = NULL;
    return HAL_OK;
}

int mpu9250_get_status(mpu9250_handle_t *handle, uint8_t *int_status)
{
    if (handle == NULL || int_status == NULL || !handle->initialized) { return (int)HAL_INVALID_PARAM; }
    return mpu9250_bus_read(handle, MPU9250_REG_INT_STATUS, int_status, 1U);
}

int mpu9250_read_raw(mpu9250_handle_t *handle, int16_t accel[3], int16_t gyro[3], int16_t *temperature_raw)
{
    uint8_t d[MPU9250_DATA_BURST_LEN];
    if (handle == NULL || accel == NULL || gyro == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (mpu9250_bus_read(handle, MPU9250_REG_ACCEL_XOUT_H, d, sizeof(d)) != HAL_OK) { return (int)HAL_IO_ERROR; }
    accel[0] = hal_be16(&d[0]);
    accel[1] = hal_be16(&d[2]);
    accel[2] = hal_be16(&d[4]);
    gyro[0] = hal_be16(&d[8]);
    gyro[1] = hal_be16(&d[10]);
    gyro[2] = hal_be16(&d[12]);
    if (temperature_raw != NULL) { *temperature_raw = hal_be16(&d[6]); }
    /* 全 0 数据说明总线读回无效（器件掉电或悬空） */
    if (accel[0] == 0 && accel[1] == 0 && accel[2] == 0 && gyro[0] == 0 && gyro[1] == 0 && gyro[2] == 0) {
        LOG_W("mpu9250", "all-zero data");
        return (int)HAL_HW_ERROR;
    }
    return HAL_OK;
}

int mpu9250_read_mag_raw(mpu9250_handle_t *handle, int16_t mag[3])
{
    uint8_t d[AK8963_MAG_BURST_LEN], st1 = 0U;
    uint32_t start = hal_tick_get();
    if (handle == NULL || mag == NULL || !handle->initialized) { return (int)HAL_INVALID_PARAM; }
    if (!handle->mag_present) { return (int)HAL_NOT_SUPPORTED; }
    /* 连续测量模式：轮询 ST1 的 DRDY 位 */
    do {
        if (hal_i2c_dev_read_reg(&handle->mag, AK8963_REG_ST1, &st1, 1U) != HAL_OK) { return (int)HAL_IO_ERROR; }
        if ((st1 & AK8963_ST1_DRDY) != 0U) { break; }
        hal_delay_ms(1U);
    } while ((uint32_t)(hal_tick_get() - start) <= MPU9250_MAG_TIMEOUT_MS);
    if ((st1 & AK8963_ST1_DRDY) == 0U) { return (int)HAL_TIMEOUT; }
    if (hal_i2c_dev_read_reg(&handle->mag, AK8963_REG_ST1, d, sizeof(d)) != HAL_OK) { return (int)HAL_IO_ERROR; }
    /* ST2 bit3 HOFL：磁场溢出，数据不可信 */
    if ((d[7] & AK8963_ST2_HOFL) != 0U) {
        LOG_W("mpu9250", "ak8963 overflow");
        return (int)HAL_OVERFLOW;
    }
    mag[0] = hal_le16(&d[1]);
    mag[1] = hal_le16(&d[3]);
    mag[2] = hal_le16(&d[5]);
    return HAL_OK;
}

int mpu9250_read(mpu9250_handle_t *handle, mpu9250_data_t *data)
{
    int16_t accel[3], gyro[3], mag[3] = { 0, 0, 0 };
    int16_t temp = 0;
    int ret;
    if (handle == NULL || data == NULL) { return (int)HAL_INVALID_PARAM; }
    ret = mpu9250_read_raw(handle, accel, gyro, &temp);
    if (ret != HAL_OK) { return ret; }
    data->accel_g.x = (float)accel[0] / handle->accel_lsb_per_g;
    data->accel_g.y = (float)accel[1] / handle->accel_lsb_per_g;
    data->accel_g.z = (float)accel[2] / handle->accel_lsb_per_g;
    data->gyro_dps.x = (float)gyro[0] / handle->gyro_lsb_per_dps;
    data->gyro_dps.y = (float)gyro[1] / handle->gyro_lsb_per_dps;
    data->gyro_dps.z = (float)gyro[2] / handle->gyro_lsb_per_dps;
    data->temperature_c = (float)temp / MPU9250_TEMP_LSB_PER_DEGC + MPU9250_TEMP_OFFSET_DEGC;
    data->mag_ut = (mpu9250_vec3_t){ 0.0f, 0.0f, 0.0f };
    data->heading_deg = 0.0f; data->mag_valid = false;
    if (handle->mag_present && mpu9250_read_mag_raw(handle, mag) == HAL_OK) {
        data->mag_ut.x = (float)mag[0] * handle->mag_ut_per_lsb[0];
        data->mag_ut.y = (float)mag[1] * handle->mag_ut_per_lsb[1];
        data->mag_ut.z = (float)mag[2] * handle->mag_ut_per_lsb[2];
        data->heading_deg = mpu9250_heading_deg(data->mag_ut.x, data->mag_ut.y);
        data->mag_valid = true;
    }
    return HAL_OK;
}
