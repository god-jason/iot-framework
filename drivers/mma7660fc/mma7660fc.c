#include "mma7660fc.h"
#include "mma7660fc_register.h"
#include "log.h"

static int mma7660fc_write(mma7660fc_handle_t *handle, uint8_t reg, uint8_t value)
{
    return (driver_i2c_write_reg(&handle->i2c, reg, &value, 1U) == DRIVER_OK) ? DRIVER_OK
                                                                              : (int)HAL_IO_ERROR;
}

static int mma7660fc_read_regs(mma7660fc_handle_t *handle, uint8_t reg, uint8_t *buf, size_t len)
{
    return (driver_i2c_read_reg(&handle->i2c, reg, buf, len) == DRIVER_OK) ? DRIVER_OK
                                                                          : (int)HAL_IO_ERROR;
}

/* 6 位补码 → int16 */
static int16_t mma7660fc_decode(uint8_t raw)
{
    int16_t value = (int16_t)(raw & MMA7660FC_DATA_MASK);
    if ((value & MMA7660FC_DATA_SIGN) != 0) { value = (int16_t)(value - 0x40); }
    return value;
}

int mma7660fc_set_sample_rate(mma7660fc_handle_t *handle, mma7660fc_sample_rate_t sample_rate)
{
    uint8_t check = 0U;
    bool was_active;
    if (handle == NULL || handle->i2c.handle == NULL ||
        (unsigned)sample_rate >= (unsigned)MMA7660FC_SR_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }
    /* SR 只能在待机模式下修改，已初始化时需临时切回待机 */
    was_active = handle->initialized;
    if (was_active &&
        mma7660fc_write(handle, MMA7660FC_REG_MODE, MMA7660FC_MODE_STANDBY) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (was_active) { driver_delay_ms(MMA7660FC_MODE_DELAY_MS); }
    if (mma7660fc_write(handle, MMA7660FC_REG_SR, (uint8_t)sample_rate) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (mma7660fc_read_regs(handle, MMA7660FC_REG_SR, &check, 1U) != DRIVER_OK ||
        (check & MMA7660FC_SR_AMSR_MASK) != (uint8_t)sample_rate) {
        LOG_E("mma7660fc", "sample rate readback 0x%02X mismatch", check);
        return (int)HAL_HW_ERROR;
    }
    handle->sample_rate = sample_rate;
    if (was_active) {
        if (mma7660fc_write(handle, MMA7660FC_REG_MODE, MMA7660FC_MODE_ACTIVE) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        driver_delay_ms(MMA7660FC_MODE_DELAY_MS);
    }
    return DRIVER_OK;
}

int mma7660fc_init(mma7660fc_handle_t *handle, hal_i2c_t *i2c, uint16_t address,
                   mma7660fc_sample_rate_t sample_rate)
{
    uint8_t check = 0U;
    if (handle == NULL || i2c == NULL ||
        (unsigned)sample_rate >= (unsigned)MMA7660FC_SR_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->i2c = (driver_i2c_t){ .handle = i2c, .timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT,
                                  .address = (address != 0U) ? address : MMA7660FC_ADDRESS };
    handle->sample_rate = sample_rate;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("mma7660fc", "no ack at 0x%02X", (int)handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }
    /* 配置寄存器只能在待机模式下修改 */
    if (mma7660fc_write(handle, MMA7660FC_REG_MODE, MMA7660FC_MODE_STANDBY) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(MMA7660FC_MODE_DELAY_MS);

    /* 无 WHO_AM_I：用 PDET 寄存器写入/回读校验芯片在位与写通路 */
    if (mma7660fc_write(handle, MMA7660FC_REG_PDET, MMA7660FC_VERIFY_PATTERN) != DRIVER_OK ||
        mma7660fc_read_regs(handle, MMA7660FC_REG_PDET, &check, 1U) != DRIVER_OK ||
        check != MMA7660FC_VERIFY_PATTERN) {
        LOG_E("mma7660fc", "id check failed (pdet readback 0x%02X)", check);
        return (int)HAL_NOT_FOUND;
    }
    (void)mma7660fc_write(handle, MMA7660FC_REG_PDET, 0x00U);

    if (mma7660fc_set_sample_rate(handle, sample_rate) != DRIVER_OK) { return (int)HAL_HW_ERROR; }

    /* 进入工作模式并回读确认 */
    if (mma7660fc_write(handle, MMA7660FC_REG_MODE, MMA7660FC_MODE_ACTIVE) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(MMA7660FC_MODE_DELAY_MS);
    if (mma7660fc_read_regs(handle, MMA7660FC_REG_MODE, &check, 1U) != DRIVER_OK ||
        (check & MMA7660FC_MODE_BIT_ACTIVE) == 0U) {
        LOG_E("mma7660fc", "failed to enter active mode (mode 0x%02X)", check);
        return (int)HAL_HW_ERROR;
    }

    handle->initialized = true;
    LOG_I("mma7660fc", "init ok at 0x%02X, sample_rate=%d", (int)handle->i2c.address,
          (int)sample_rate);
    return DRIVER_OK;
}

int mma7660fc_deinit(mma7660fc_handle_t *handle)
{
    if (handle == NULL) { return (int)HAL_INVALID_PARAM; }
    if (handle->initialized) {
        (void)mma7660fc_write(handle, MMA7660FC_REG_MODE, MMA7660FC_MODE_STANDBY);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int mma7660fc_get_tilt(mma7660fc_handle_t *handle, uint8_t *tilt)
{
    if (handle == NULL || tilt == NULL || !handle->initialized) { return (int)HAL_INVALID_PARAM; }
    return mma7660fc_read_regs(handle, MMA7660FC_REG_TILT, tilt, 1U);
}

int mma7660fc_read_raw(mma7660fc_handle_t *handle, int16_t xyz[3])
{
    uint8_t d[3];
    if (handle == NULL || xyz == NULL || !handle->initialized) { return (int)HAL_INVALID_PARAM; }
    /* XOUT/YOUT/ZOUT 连续三个寄存器 */
    if (mma7660fc_read_regs(handle, MMA7660FC_REG_XOUT, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    xyz[0] = mma7660fc_decode(d[0]);
    xyz[1] = mma7660fc_decode(d[1]);
    xyz[2] = mma7660fc_decode(d[2]);
    return DRIVER_OK;
}

int mma7660fc_read(mma7660fc_handle_t *handle, mma7660fc_data_t *data)
{
    uint8_t d[3];
    if (handle == NULL || data == NULL || !handle->initialized) { return (int)HAL_INVALID_PARAM; }
    if (mma7660fc_read_regs(handle, MMA7660FC_REG_XOUT, d, sizeof(d)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 21.33 counts/g → g */
    data->accel_g.x = (float)mma7660fc_decode(d[0]) / MMA7660FC_COUNTS_PER_G;
    data->accel_g.y = (float)mma7660fc_decode(d[1]) / MMA7660FC_COUNTS_PER_G;
    data->accel_g.z = (float)mma7660fc_decode(d[2]) / MMA7660FC_COUNTS_PER_G;
    data->alert = ((d[0] & MMA7660FC_DATA_ALERT) != 0U);
    if (mma7660fc_get_tilt(handle, &data->tilt) != DRIVER_OK) { data->tilt = 0U; }
    return DRIVER_OK;
}
