#include "bmm150.h"
#include "bmm150_register.h"
#include "log.h"
#include <math.h>

static int bmm150_write(bmm150_handle_t *handle, uint8_t reg, uint8_t value)
{
    uint8_t tx[2];
    int ret;
    if (!handle->use_spi) {
        return (driver_i2c_write_reg(&handle->i2c, reg, &value, 1U) == DRIVER_OK) ? DRIVER_OK : (int)IOT_IO_ERROR;
    }
    /* BMM150 SPI 写：地址最高位为 0 */
    tx[0] = (uint8_t)(reg & 0x7FU);
    tx[1] = value;
    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, NULL, sizeof(tx));
    driver_spi_deselect(&handle->spi);
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)IOT_IO_ERROR;
}

static int bmm150_read_reg(bmm150_handle_t *handle, uint8_t reg, uint8_t *buf, size_t len)
{
    uint8_t cmd;
    int ret;
    if (len == 0U || len > BMM150_BURST_MAX) { return (int)IOT_INVALID_PARAM; }
    if (!handle->use_spi) {
        return (driver_i2c_read_reg(&handle->i2c, reg, buf, len) == DRIVER_OK) ? DRIVER_OK : (int)IOT_IO_ERROR;
    }
    /* BMM150 SPI 读：地址最高位置 1 */
    cmd = (uint8_t)(reg | BMM150_SPI_READ);
    driver_spi_select(&handle->spi);
    ret = driver_spi_write(&handle->spi, &cmd, 1U);
    if (ret == DRIVER_OK) { ret = driver_spi_read(&handle->spi, buf, len); }
    driver_spi_deselect(&handle->spi);
    return (ret == DRIVER_OK) ? DRIVER_OK : (int)IOT_IO_ERROR;
}

int bmm150_get_status(bmm150_handle_t *handle, uint8_t *int_status)
{
    if (handle == NULL || int_status == NULL || !handle->initialized) { return (int)IOT_INVALID_PARAM; }
    return bmm150_read_reg(handle, BMM150_REG_INT_STATUS, int_status, 1U);
}

int bmm150_init(bmm150_handle_t *handle, iot_i2c_t *i2c,
                iot_spi_t *spi, iot_pin_t cs, uint16_t address)
{
    uint8_t id = 0U;
    uint8_t value;
    if (handle == NULL || (i2c == NULL && spi == NULL)) { return (int)IOT_INVALID_PARAM; }
    handle->use_spi = (spi != NULL);
    handle->i2c = (driver_i2c_t){ .handle = i2c, .timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT,
                                  .address = (address != 0U) ? address : BMM150_DEFAULT_ADDRESS };
    handle->spi = (driver_spi_t){ .handle = spi, .cs = cs, .timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT };
    handle->offset_ut[0] = 0.0f;
    handle->offset_ut[1] = 0.0f;
    handle->offset_ut[2] = 0.0f;
    handle->initialized = false;

    /* CHIP_ID 在位校验 */
    if (bmm150_read_reg(handle, BMM150_REG_CHIP_ID, &id, 1U) != DRIVER_OK) { return (int)IOT_IO_ERROR; }
    if (id != BMM150_CHIP_ID_VALUE) {
        LOG_E("bmm150", "chip id 0x%02X mismatch", id);
        return (int)IOT_NOT_FOUND;
    }

    /* 软复位后重新读 ID，确认复位与写通路均正常 */
    value = BMM150_POWER_CONTROL_RESET;
    if (bmm150_write(handle, BMM150_REG_POWER_CONTROL, value) != DRIVER_OK) { return (int)IOT_IO_ERROR; }
    driver_delay_ms(BMM150_RESET_DELAY_MS);
    if (bmm150_read_reg(handle, BMM150_REG_CHIP_ID, &id, 1U) != DRIVER_OK || id != BMM150_CHIP_ID_VALUE) {
        LOG_E("bmm150", "chip id lost after reset (0x%02X)", id);
        return (int)IOT_HW_ERROR;
    }

    /* 上电 + 三轴使能；重复次数保持复位默认值 */
    if (bmm150_write(handle, BMM150_REG_POWER_CONTROL, BMM150_POWER_CONTROL_ON) != DRIVER_OK ||
        bmm150_write(handle, BMM150_REG_AXES_ENABLE, BMM150_AXES_ENABLE_ALL) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(BMM150_POWER_ON_DELAY_MS);

    handle->initialized = true;
    LOG_I("bmm150", "init ok bus=%s addr=0x%02X", handle->use_spi ? "spi" : "i2c",
          (int)handle->i2c.address);
    return DRIVER_OK;
}

int bmm150_deinit(bmm150_handle_t *handle)
{
    if (handle == NULL) { return (int)IOT_INVALID_PARAM; }
    if (handle->initialized) {
        (void)bmm150_write(handle, BMM150_REG_OP_MODE, BMM150_OP_MODE_SUSPEND);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    handle->spi.handle = NULL;
    return DRIVER_OK;
}

/* 触发一次 forced 测量并读取原始数据 */
static int bmm150_measure(bmm150_handle_t *handle, int16_t xyz[3], uint16_t *rhall)
{
    uint8_t d[BMM150_DATA_BURST_LEN];
    uint8_t status = 0U;
    int16_t x;
    int16_t y;
    int16_t z;

    if (bmm150_write(handle, BMM150_REG_OP_MODE, BMM150_OP_MODE_FORCED) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(BMM150_FORCED_DELAY_MS);
    if (bmm150_read_reg(handle, BMM150_REG_INT_STATUS, &status, 1U) != DRIVER_OK) { return (int)IOT_IO_ERROR; }
    if ((status & BMM150_INT_STATUS_DRDY) == 0U) { return (int)IOT_NOT_READY; }

    if (bmm150_read_reg(handle, BMM150_REG_DATA_X_LSB, d, sizeof(d)) != DRIVER_OK) { return (int)IOT_IO_ERROR; }

    /* X/Y：13 位有符号（bit15:3） */
    x = (int16_t)(((uint16_t)d[1] << 5) | ((uint16_t)d[0] >> 3));
    y = (int16_t)(((uint16_t)d[3] << 5) | ((uint16_t)d[2] >> 3));
    if (x > 4095) { x = (int16_t)(x - 8192); }
    if (y > 4095) { y = (int16_t)(y - 8192); }
    /* Z：15 位有符号（bit15:1） */
    z = (int16_t)(((uint16_t)d[5] << 7) | ((uint16_t)d[4] >> 1));
    if (z > 16383) { z = (int16_t)(z - 32768); }

    xyz[0] = x;
    xyz[1] = y;
    xyz[2] = z;
    if (rhall != NULL) {
        *rhall = (uint16_t)((((uint16_t)d[7] << 6) | ((uint16_t)d[6] >> 2)) & 0x3FFFU);
    }
    return DRIVER_OK;
}

int bmm150_read_raw(bmm150_handle_t *handle, int16_t xyz[3])
{
    if (handle == NULL || xyz == NULL || !handle->initialized) { return (int)IOT_INVALID_PARAM; }
    return bmm150_measure(handle, xyz, NULL);
}

int bmm150_read(bmm150_handle_t *handle, bmm150_data_t *data)
{
    int16_t xyz[3];
    uint16_t rhall = 0U;
    int ret;
    if (handle == NULL || data == NULL) { return (int)IOT_INVALID_PARAM; }
    ret = bmm150_measure(handle, xyz, &rhall);
    if (ret != DRIVER_OK) { return ret; }

    /*
     * 标称灵敏度换算并扣除软件硬铁偏置。
     * 说明：Bosch 参考实现会结合 trim 寄存器（0x5D~0x6C）与 RHALL 做多项式
     * 补偿以获得更高精度；此处为便于移植的基础实现，误差约 ±3%。
     * 需要高精度时，调用方应先做椭球标定，再用 bmm150_set_calibration 扣除偏置。
     */
    data->mag_ut.x = (float)xyz[0] * BMM150_UT_PER_LSB_XY - handle->offset_ut[0];
    data->mag_ut.y = (float)xyz[1] * BMM150_UT_PER_LSB_XY - handle->offset_ut[1];
    data->mag_ut.z = (float)xyz[2] * BMM150_UT_PER_LSB_Z - handle->offset_ut[2];
    data->rhall = rhall;

    data->heading_deg = atan2f(data->mag_ut.y, data->mag_ut.x) * 57.2957795f;
    if (data->heading_deg < 0.0f) { data->heading_deg += 360.0f; }
    return DRIVER_OK;
}

int bmm150_set_calibration(bmm150_handle_t *handle, float offset_x, float offset_y, float offset_z)
{
    if (handle == NULL || !handle->initialized) { return (int)IOT_INVALID_PARAM; }
    handle->offset_ut[0] = offset_x;
    handle->offset_ut[1] = offset_y;
    handle->offset_ut[2] = offset_z;
    return DRIVER_OK;
}
