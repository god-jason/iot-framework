#include "l3gd20h.h"
#include "l3gd20h_register.h"
#include "log.h"

/* 按量程索引的灵敏度：mdps/LSB */
static const float s_mdps_per_lsb[L3GD20H_RANGE_COUNT] = {
    L3GD20H_MDPS_PER_LSB_245, L3GD20H_MDPS_PER_LSB_500, L3GD20H_MDPS_PER_LSB_2000
};

static int l3gd20h_write(l3gd20h_handle_t *handle, uint8_t reg, uint8_t value)
{
    uint8_t tx[2];
    int ret;
    if (!handle->use_spi) {
        return (driver_i2c_write_reg(&handle->i2c, reg, &value, 1U) == DRIVER_OK) ? DRIVER_OK : (int)PORT_IO_ERROR;
    }
    tx[0] = (uint8_t)(reg & 0x7FU);
    tx[1] = value;
    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, NULL, sizeof(tx));
    driver_spi_deselect(&handle->spi);
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)PORT_IO_ERROR;
}

/* 多字节读：I2C 需置 sub-address bit7 使能自增，SPI 用 bit7 读 + bit6 自增 */
static int l3gd20h_bus_read(l3gd20h_handle_t *handle, uint8_t reg, uint8_t *buf, size_t len)
{
    uint8_t cmd;
    int ret;
    if (len == 0U || len > L3GD20H_BURST_MAX) { return (int)PORT_INVALID_PARAM; }
    if (!handle->use_spi) {
        cmd = (len > 1U) ? (uint8_t)(reg | L3GD20H_I2C_AUTO_INC) : reg;
        return (driver_i2c_read_reg(&handle->i2c, cmd, buf, len) == DRIVER_OK) ? DRIVER_OK : (int)PORT_IO_ERROR;
    }
    cmd = (uint8_t)(reg | L3GD20H_SPI_READ | ((len > 1U) ? L3GD20H_SPI_AUTO_INC : 0U));
    driver_spi_select(&handle->spi);
    ret = driver_spi_write(&handle->spi, &cmd, 1U);
    if (ret == DRIVER_OK) { ret = driver_spi_read(&handle->spi, buf, len); }
    driver_spi_deselect(&handle->spi);
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)PORT_IO_ERROR;
}

int l3gd20h_get_status(l3gd20h_handle_t *handle, uint8_t *status)
{
    if (handle == NULL || status == NULL || !handle->initialized) { return (int)PORT_INVALID_PARAM; }
    return l3gd20h_bus_read(handle, L3GD20H_REG_STATUS, status, 1U);
}

int l3gd20h_init(l3gd20h_handle_t *handle, port_i2c_handle_t *i2c,
                 port_spi_handle_t *spi, port_pin_t cs, uint16_t address,
                 l3gd20h_range_t range, l3gd20h_odr_t odr)
{
    uint8_t id = 0U;
    uint8_t check = 0U;
    uint8_t value;
    if (handle == NULL || (i2c == NULL && spi == NULL) ||
        (unsigned)range >= (unsigned)L3GD20H_RANGE_COUNT ||
        (unsigned)odr >= (unsigned)L3GD20H_ODR_COUNT) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->use_spi = (spi != NULL);
    handle->i2c = (driver_i2c_t){ .handle = i2c, .timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT,
                                  .address = (address != 0U) ? address : L3GD20H_DEFAULT_ADDRESS };
    handle->spi = (driver_spi_t){ .handle = spi, .cs = cs, .timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT };
    handle->range = range; handle->odr = odr;
    handle->mdps_per_lsb = s_mdps_per_lsb[range];
    handle->initialized = false;

    /* WHO_AM_I 在位校验 */
    if (l3gd20h_bus_read(handle, L3GD20H_REG_WHO_AM_I, &id, 1U) != DRIVER_OK) { return (int)PORT_IO_ERROR; }
    if (id != L3GD20H_WHO_AM_I_VALUE) {
        LOG_E("l3gd20h", "who_am_i 0x%02X mismatch", id);
        return (int)PORT_NOT_FOUND;
    }

    /* CTRL1：输出率 + 带宽 12.5Hz + 正常模式 + 三轴使能 */
    value = (uint8_t)(((uint8_t)odr << L3GD20H_CTRL1_ODR_SHIFT) |
                      (L3GD20H_CTRL1_BW_12HZ5 << L3GD20H_CTRL1_BW_SHIFT) |
                      L3GD20H_CTRL1_PD | L3GD20H_CTRL1_AXES_EN);
    if (l3gd20h_write(handle, L3GD20H_REG_CTRL1, value) != DRIVER_OK) { return (int)PORT_IO_ERROR; }

    /* CTRL4：块数据更新 + 量程 */
    value = (uint8_t)(L3GD20H_CTRL4_BDU | ((uint8_t)range << L3GD20H_CTRL4_FS_SHIFT));
    if (l3gd20h_write(handle, L3GD20H_REG_CTRL4, value) != DRIVER_OK) { return (int)PORT_IO_ERROR; }
    driver_delay_ms(L3GD20H_BOOT_DELAY_MS);

    /* 回读校验量程位，同时确认 STATUS 不是总线悬空的全 1 */
    if (l3gd20h_bus_read(handle, L3GD20H_REG_CTRL4, &check, 1U) != DRIVER_OK ||
        (check & L3GD20H_CTRL4_FS_MASK) != (uint8_t)((uint8_t)range << L3GD20H_CTRL4_FS_SHIFT)) {
        LOG_E("l3gd20h", "range readback 0x%02X mismatch", check);
        return (int)PORT_HW_ERROR;
    }
    if (l3gd20h_bus_read(handle, L3GD20H_REG_STATUS, &check, 1U) != DRIVER_OK || check == 0xFFU) {
        LOG_E("l3gd20h", "status invalid 0x%02X", check);
        return (int)PORT_HW_ERROR;
    }

    handle->initialized = true;
    LOG_I("l3gd20h", "init ok bus=%s range=%d odr=%d", handle->use_spi ? "spi" : "i2c",
          (int)range, (int)odr);
    return DRIVER_OK;
}

int l3gd20h_deinit(l3gd20h_handle_t *handle)
{
    if (handle == NULL) { return (int)PORT_INVALID_PARAM; }
    if (handle->initialized) {
        (void)l3gd20h_write(handle, L3GD20H_REG_CTRL1, L3GD20H_CTRL1_POWER_DOWN);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    handle->spi.handle = NULL;
    return DRIVER_OK;
}

int l3gd20h_read_raw(l3gd20h_handle_t *handle, int16_t gyro[3], int16_t *temperature_raw)
{
    uint8_t d[6];
    uint8_t status = 0U;
    uint8_t temp = 0U;
    uint32_t start;
    if (handle == NULL || gyro == NULL || !handle->initialized) { return (int)PORT_INVALID_PARAM; }

    /* 等待 ZYXDA 数据就绪，避免读到旧样本 */
    start = driver_tick_ms();
    do {
        if (l3gd20h_bus_read(handle, L3GD20H_REG_STATUS, &status, 1U) != DRIVER_OK) { return (int)PORT_IO_ERROR; }
        if ((status & L3GD20H_STATUS_ZYXDA) != 0U) { break; }
        driver_delay_ms(1U);
    } while ((uint32_t)(driver_tick_ms() - start) <= L3GD20H_DATA_TIMEOUT_MS);
    if ((status & L3GD20H_STATUS_ZYXDA) == 0U) { return (int)PORT_TIMEOUT; }
    if ((status & L3GD20H_STATUS_ZYXOR) != 0U) { LOG_W("l3gd20h", "data overrun"); }

    if (l3gd20h_bus_read(handle, L3GD20H_REG_OUT_X_L, d, sizeof(d)) != DRIVER_OK) { return (int)PORT_IO_ERROR; }
    gyro[0] = driver_le16(&d[0]);
    gyro[1] = driver_le16(&d[2]);
    gyro[2] = driver_le16(&d[4]);

    if (temperature_raw != NULL) {
        if (l3gd20h_bus_read(handle, L3GD20H_REG_OUT_TEMP, &temp, 1U) != DRIVER_OK) { return (int)PORT_IO_ERROR; }
        *temperature_raw = (int16_t)(int8_t)temp;   /* 8 位补码 */
    }
    return DRIVER_OK;
}

int l3gd20h_read(l3gd20h_handle_t *handle, l3gd20h_data_t *data)
{
    int16_t gyro[3];
    int16_t temp = 0;
    int ret;
    if (handle == NULL || data == NULL) { return (int)PORT_INVALID_PARAM; }
    ret = l3gd20h_read_raw(handle, gyro, &temp);
    if (ret != DRIVER_OK) { return ret; }

    /* mdps/LSB → °/s */
    data->dps.x = (float)gyro[0] * handle->mdps_per_lsb / 1000.0f;
    data->dps.y = (float)gyro[1] * handle->mdps_per_lsb / 1000.0f;
    data->dps.z = (float)gyro[2] * handle->mdps_per_lsb / 1000.0f;
    data->temperature_c = L3GD20H_TEMP_OFFSET_DEGC + (float)temp;
    return DRIVER_OK;
}
