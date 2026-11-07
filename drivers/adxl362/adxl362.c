#include "adxl362.h"
#include "adxl362_register.h"
#include "log.h"

static int adxl362_bus_read(adxl362_handle_t *handle, uint8_t reg, uint8_t *buf, size_t len)
{
    uint8_t tx[ADXL362_BURST_MAX + 2U] = { 0U };
    uint8_t rx[ADXL362_BURST_MAX + 2U];
    size_t i;
    int ret;
    if (len == 0U || len > ADXL362_BURST_MAX) { return (int)PORT_INVALID_PARAM; }

    tx[0] = ADXL362_CMD_READ;
    tx[1] = reg;
    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, rx, len + 2U);
    driver_spi_deselect(&handle->spi);
    if (ret != DRIVER_OK) { return (int)PORT_IO_ERROR; }
    for (i = 0U; i < len; i++) { buf[i] = rx[i + 2U]; }
    return DRIVER_OK;
}

static int adxl362_bus_write(adxl362_handle_t *handle, uint8_t reg, uint8_t value)
{
    uint8_t tx[3];
    int ret;
    tx[0] = ADXL362_CMD_WRITE;
    tx[1] = reg;
    tx[2] = value;
    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, NULL, sizeof(tx));
    driver_spi_deselect(&handle->spi);
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)PORT_IO_ERROR;
}

/* 12 位有符号值：小端存放，高 4 位无效 */
static int16_t adxl362_le12(const uint8_t *p)
{
    int16_t value = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    value &= 0x0FFF;
    if ((value & 0x0800) != 0) { value = (int16_t)(value | (int16_t)0xF000); }
    return value;
}

int adxl362_get_status(adxl362_handle_t *handle, uint8_t *status)
{
    if (handle == NULL || status == NULL || !handle->initialized) { return (int)PORT_INVALID_PARAM; }
    return adxl362_bus_read(handle, ADXL362_REG_STATUS, status, 1U);
}

int adxl362_init(adxl362_handle_t *handle, port_spi_handle_t *spi, port_pin_t cs,
                 adxl362_range_t range, adxl362_odr_t odr)
{
    uint8_t id[ADXL362_ID_LEN];
    uint8_t value;
    uint8_t check = 0U;
    if (handle == NULL || spi == NULL || (unsigned)range >= (unsigned)ADXL362_RANGE_COUNT ||
        (unsigned)odr < (unsigned)ADXL362_ODR_12HZ5 || (unsigned)odr > (unsigned)ADXL362_ODR_400HZ) {
        return (int)PORT_INVALID_PARAM;
    }
    handle->spi = (driver_spi_t){ .handle = spi, .cs = cs, .timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT };
    handle->range = range; handle->odr = odr;
    handle->initialized = false;

    /* 软复位，恢复默认寄存器 */
    if (adxl362_bus_write(handle, ADXL362_REG_SOFT_RESET, ADXL362_RESET_VALUE) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(ADXL362_RESET_DELAY_MS);

    /* 身份校验：ADXL362 无 WHO_AM_I，用 DEVID_AD/DEVID_MST/PARTID 代替 */
    if (adxl362_bus_read(handle, ADXL362_REG_DEVID_AD, id, sizeof(id)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (id[0] != ADXL362_DEVID_AD_VALUE || id[1] != ADXL362_DEVID_MST_VALUE ||
        id[2] != ADXL362_PARTID_VALUE) {
        LOG_E("adxl362", "device id mismatch %02X %02X %02X", id[0], id[1], id[2]);
        return (int)PORT_NOT_FOUND;
    }

    /* FILTER_CTL：量程 + 输出率 */
    value = (uint8_t)(((uint8_t)range << ADXL362_FILTER_RANGE_SHIFT) | (uint8_t)odr);
    if (adxl362_bus_write(handle, ADXL362_REG_FILTER_CTL, value) != DRIVER_OK ||
        adxl362_bus_write(handle, ADXL362_REG_POWER_CTL, ADXL362_POWER_MEASURE) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    driver_delay_ms(ADXL362_START_DELAY_MS);

    /* 回读校验 + 用户寄存器错误检查 */
    if (adxl362_bus_read(handle, ADXL362_REG_FILTER_CTL, &check, 1U) != DRIVER_OK ||
        (check & ADXL362_FILTER_RANGE_MASK) != (uint8_t)((uint8_t)range << ADXL362_FILTER_RANGE_SHIFT) ||
        (check & ADXL362_FILTER_ODR_MASK) != (uint8_t)odr) {
        LOG_E("adxl362", "filter_ctl readback 0x%02X mismatch", check);
        return (int)PORT_HW_ERROR;
    }
    if (adxl362_bus_read(handle, ADXL362_REG_STATUS, &check, 1U) != DRIVER_OK ||
        (check & ADXL362_STATUS_ERR_USER_REGS) != 0U) {
        LOG_E("adxl362", "user regs error, status 0x%02X", check);
        return (int)PORT_HW_ERROR;
    }

    handle->initialized = true;
    LOG_I("adxl362", "init ok range=%d odr=0x%02X", (int)range, (int)odr);
    return DRIVER_OK;
}

int adxl362_deinit(adxl362_handle_t *handle)
{
    if (handle == NULL) { return (int)PORT_INVALID_PARAM; }
    if (handle->initialized) {
        (void)adxl362_bus_write(handle, ADXL362_REG_POWER_CTL, ADXL362_POWER_STANDBY);
    }
    handle->initialized = false;
    handle->spi.handle = NULL;
    return DRIVER_OK;
}

int adxl362_read_raw(adxl362_handle_t *handle, int16_t xyz[3], int16_t *temperature_raw)
{
    uint8_t d[6];
    uint8_t t[2];
    uint8_t status = 0U;
    if (handle == NULL || xyz == NULL || !handle->initialized) { return (int)PORT_INVALID_PARAM; }

    if (adxl362_bus_read(handle, ADXL362_REG_STATUS, &status, 1U) != DRIVER_OK) { return (int)PORT_IO_ERROR; }
    if ((status & ADXL362_STATUS_OVR) != 0U) { LOG_W("adxl362", "data overflow"); }

    if (adxl362_bus_read(handle, ADXL362_REG_XDATA_L, d, sizeof(d)) != DRIVER_OK) { return (int)PORT_IO_ERROR; }
    xyz[0] = adxl362_le12(&d[0]);
    xyz[1] = adxl362_le12(&d[2]);
    xyz[2] = adxl362_le12(&d[4]);

    if (temperature_raw != NULL) {
        if (adxl362_bus_read(handle, ADXL362_REG_TEMP_L, t, sizeof(t)) != DRIVER_OK) { return (int)PORT_IO_ERROR; }
        *temperature_raw = adxl362_le12(t);
    }
    return DRIVER_OK;
}

int adxl362_read(adxl362_handle_t *handle, adxl362_data_t *data)
{
    int16_t xyz[3];
    int16_t temp = 0;
    uint8_t status = 0U;
    int ret;
    if (handle == NULL || data == NULL) { return (int)PORT_INVALID_PARAM; }
    ret = adxl362_read_raw(handle, xyz, &temp);
    if (ret != DRIVER_OK) { return ret; }
    if (adxl362_get_status(handle, &status) != DRIVER_OK) { return (int)PORT_IO_ERROR; }

    data->accel_g.x = (float)xyz[0] * ADXL362_G_PER_LSB;
    data->accel_g.y = (float)xyz[1] * ADXL362_G_PER_LSB;
    data->accel_g.z = (float)xyz[2] * ADXL362_G_PER_LSB;
    data->temperature_c = (float)temp * ADXL362_DEGC_PER_LSB;
    data->data_ready = ((status & ADXL362_STATUS_DATA_RDY) != 0U);
    data->overflow = ((status & ADXL362_STATUS_OVR) != 0U);
    return DRIVER_OK;
}
