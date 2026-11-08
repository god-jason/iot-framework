#include "adxl375.h"
#include "adxl375_register.h"
#include "log.h"

static int adxl375_write(adxl375_handle_t *handle, uint8_t reg, uint8_t value)
{
    uint8_t tx[2];
    int ret;
    if (!handle->use_spi) {
        return (driver_i2c_write_reg(&handle->i2c, reg, &value, 1U) == DRIVER_OK) ? DRIVER_OK : (int)HAL_IO_ERROR;
    }
    /* SPI 写：地址 bit7/bit6 必须为 0 */
    tx[0] = (uint8_t)(reg & 0x3FU);
    tx[1] = value;
    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, NULL, sizeof(tx));
    driver_spi_deselect(&handle->spi);
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)HAL_IO_ERROR;
}

static int adxl375_read_regs(adxl375_handle_t *handle, uint8_t reg, uint8_t *buf, size_t len)
{
    uint8_t cmd;
    int ret;
    if (len == 0U || len > ADXL375_BURST_MAX) { return (int)HAL_INVALID_PARAM; }
    if (!handle->use_spi) {
        return (driver_i2c_read_reg(&handle->i2c, reg, buf, len) == DRIVER_OK) ? DRIVER_OK : (int)HAL_IO_ERROR;
    }
    cmd = (uint8_t)(reg | ADXL375_SPI_READ | ((len > 1U) ? ADXL375_SPI_MULTI_BYTE : 0U));
    driver_spi_select(&handle->spi);
    ret = driver_spi_write(&handle->spi, &cmd, 1U);
    if (ret == DRIVER_OK) { ret = driver_spi_read(&handle->spi, buf, len); }
    driver_spi_deselect(&handle->spi);
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)HAL_IO_ERROR;
}

int adxl375_get_status(adxl375_handle_t *handle, uint8_t *int_source)
{
    if (handle == NULL || int_source == NULL || !handle->initialized) { return (int)HAL_INVALID_PARAM; }
    return adxl375_read_regs(handle, ADXL375_REG_INT_SOURCE, int_source, 1U);
}

int adxl375_init(adxl375_handle_t *handle, hal_i2c_t *i2c,
                 hal_spi_t *spi, hal_pin_t cs, uint16_t address, adxl375_odr_t odr)
{
    uint8_t id = 0U;
    uint8_t check = 0U;
    if (handle == NULL || (i2c == NULL && spi == NULL) ||
        ((uint8_t)odr < ADXL375_BW_RATE_100HZ) || ((uint8_t)odr > ADXL375_BW_RATE_3200HZ)) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->use_spi = (spi != NULL);
    handle->i2c = (driver_i2c_t){ .handle = i2c, .timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT,
                                  .address = (address != 0U) ? address : ADXL375_DEFAULT_ADDRESS };
    handle->spi = (driver_spi_t){ .handle = spi, .cs = cs, .timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT };
    handle->odr = odr;
    handle->initialized = false;

    if (adxl375_write(handle, ADXL375_REG_POWER_CTL, ADXL375_POWER_STANDBY) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    /* DEVID 在位校验 */
    if (adxl375_read_regs(handle, ADXL375_REG_DEVID, &id, 1U) != DRIVER_OK) { return (int)HAL_IO_ERROR; }
    if (id != ADXL375_DEVID_VALUE) {
        LOG_E("adxl375", "devid 0x%02X mismatch", id);
        return (int)HAL_NOT_FOUND;
    }

    /* 全分辨率 + ±200g；输出率；进入测量模式 */
    if (adxl375_write(handle, ADXL375_REG_DATA_FORMAT, ADXL375_FORMAT_DEFAULT) != DRIVER_OK ||
        adxl375_write(handle, ADXL375_REG_BW_RATE, (uint8_t)odr) != DRIVER_OK ||
        adxl375_write(handle, ADXL375_REG_POWER_CTL, ADXL375_POWER_MEASURE) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(ADXL375_BOOT_DELAY_MS);

    /* 写通路自检：回读 DATA_FORMAT 的全分辨率位与 BW_RATE 输出率 */
    if (adxl375_read_regs(handle, ADXL375_REG_DATA_FORMAT, &check, 1U) != DRIVER_OK ||
        (check & ADXL375_FORMAT_FULL_RES) == 0U) {
        LOG_E("adxl375", "data_format readback 0x%02X", check);
        return (int)HAL_HW_ERROR;
    }
    if (adxl375_read_regs(handle, ADXL375_REG_BW_RATE, &check, 1U) != DRIVER_OK ||
        (check & ADXL375_BW_RATE_MASK) != (uint8_t)odr) {
        LOG_E("adxl375", "bw_rate readback 0x%02X", check);
        return (int)HAL_HW_ERROR;
    }

    handle->initialized = true;
    LOG_I("adxl375", "init ok bus=%s odr=0x%02X", handle->use_spi ? "spi" : "i2c", (int)odr);
    return DRIVER_OK;
}

int adxl375_deinit(adxl375_handle_t *handle)
{
    if (handle == NULL) { return (int)HAL_INVALID_PARAM; }
    if (handle->initialized) {
        (void)adxl375_write(handle, ADXL375_REG_POWER_CTL, ADXL375_POWER_STANDBY);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    handle->spi.handle = NULL;
    return DRIVER_OK;
}

int adxl375_read_raw(adxl375_handle_t *handle, int16_t xyz[3])
{
    uint8_t d[6];
    if (handle == NULL || xyz == NULL || !handle->initialized) { return (int)HAL_INVALID_PARAM; }
    if (adxl375_read_regs(handle, ADXL375_REG_DATAX0, d, sizeof(d)) != DRIVER_OK) { return (int)HAL_IO_ERROR; }

    /* 小端：低字节在前 */
    xyz[0] = driver_le16(&d[0]);
    xyz[1] = driver_le16(&d[2]);
    xyz[2] = driver_le16(&d[4]);
    return DRIVER_OK;
}

int adxl375_read(adxl375_handle_t *handle, adxl375_data_t *data)
{
    int16_t xyz[3];
    uint8_t status = 0U;
    int ret;
    if (handle == NULL || data == NULL) { return (int)HAL_INVALID_PARAM; }
    ret = adxl375_read_raw(handle, xyz);
    if (ret != DRIVER_OK) { return ret; }
    if (adxl375_get_status(handle, &status) != DRIVER_OK) { return (int)HAL_IO_ERROR; }

    /* 20.5 LSB/g → g */
    data->accel_g.x = (float)xyz[0] / ADXL375_LSB_PER_G;
    data->accel_g.y = (float)xyz[1] / ADXL375_LSB_PER_G;
    data->accel_g.z = (float)xyz[2] / ADXL375_LSB_PER_G;
    data->data_ready = ((status & ADXL375_INT_DATA_READY) != 0U);
    data->overrun = ((status & ADXL375_INT_OVERRUN) != 0U);
    return DRIVER_OK;
}
