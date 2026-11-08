#include "ads1115.h"
#include "ads1115_register.h"
#include "log.h"

/* 各数据速率对应的单次转换时间（ms），用于轮询超时 */
static const uint16_t ads1115_conv_ms[8] = { 125U, 63U, 32U, 16U, 8U, 4U, 3U, 2U };

/* PGA 满量程（mV） */
static const uint16_t ads1115_fs_mv[6] = {
    ADS1115_FS_6V144_MV, ADS1115_FS_4V096_MV, ADS1115_FS_2V048_MV,
    ADS1115_FS_1V024_MV, ADS1115_FS_0V512_MV, ADS1115_FS_0V256_MV
};

static int ads1115_write_config(ads1115_handle_t *handle, uint16_t config)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(config >> 8);
    buf[1] = (uint8_t)(config & 0xFFU);
    if (driver_i2c_write_reg(&handle->i2c, ADS1115_REG_CONFIG, buf, 2U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

static int ads1115_read_config(ads1115_handle_t *handle, uint16_t *config)
{
    uint8_t buf[2];

    if (driver_i2c_read_reg(&handle->i2c, ADS1115_REG_CONFIG, buf, 2U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    *config = (uint16_t)(((uint16_t)buf[0] << 8) | (uint16_t)buf[1]);
    return DRIVER_OK;
}

static uint16_t ads1115_build_config(const ads1115_handle_t *handle,
                                     ads1115_mux_t mux, bool start)
{
    uint16_t config;

    config = (uint16_t)(((uint16_t)mux << ADS1115_CFG_MUX_SHIFT) & ADS1115_CFG_MUX_MASK);
    config |= (uint16_t)(((uint16_t)handle->pga << ADS1115_CFG_PGA_SHIFT) & ADS1115_CFG_PGA_MASK);
    config |= (uint16_t)(((uint16_t)handle->data_rate << ADS1115_CFG_DR_SHIFT) & ADS1115_CFG_DR_MASK);
    config |= (uint16_t)(ADS1115_CFG_MODE_SINGLE | ADS1115_CFG_COMP_QUE_OFF);
    if (start) {
        config |= ADS1115_CFG_OS;
    }
    return config;
}

int ads1115_init(ads1115_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint16_t written;
    uint16_t readback = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : ADS1115_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->pga = ADS1115_PGA_2V048;
    handle->data_rate = ADS1115_DR_128;
    handle->initialized = false;

    if (handle->i2c.address > ADS1115_ADDRESS_MAX) {
        return (int)HAL_INVALID_PARAM;
    }

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("ads1115", "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    /* 比较器阈值置为无效范围，避免 ALERT 引脚误动作；该寄存器只写，忽略写失败 */
    {
        uint8_t lo[2];
        uint8_t hi[2];

        lo[0] = (uint8_t)(ADS1115_LO_THRESH_DEFAULT >> 8);
        lo[1] = (uint8_t)(ADS1115_LO_THRESH_DEFAULT & 0xFFU);
        hi[0] = (uint8_t)(ADS1115_HI_THRESH_DEFAULT >> 8);
        hi[1] = (uint8_t)(ADS1115_HI_THRESH_DEFAULT & 0xFFU);
        (void)driver_i2c_write_reg(&handle->i2c, ADS1115_REG_LO_THRESH, lo, 2U);
        (void)driver_i2c_write_reg(&handle->i2c, ADS1115_REG_HI_THRESH, hi, 2U);
    }

    written = ads1115_build_config(handle, ADS1115_MUX_SINGLE_0, false);
    if (ads1115_write_config(handle, written) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 回读配置校验（OS 位为转换状态，不参与比较） */
    if (ads1115_read_config(handle, &readback) != DRIVER_OK) {
        return (int)HAL_NOT_FOUND;
    }
    if ((readback & (uint16_t)~ADS1115_CFG_OS) != (written & (uint16_t)~ADS1115_CFG_OS)) {
        LOG_E("ads1115", "config readback mismatch: 0x%04X", (unsigned)readback);
        return (int)HAL_NOT_FOUND;
    }

    handle->initialized = true;
    LOG_I("ads1115", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int ads1115_deinit(ads1115_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int ads1115_set_pga(ads1115_handle_t *handle, ads1115_pga_t pga)
{
    if (handle == NULL || !handle->initialized || (uint16_t)pga > (uint16_t)ADS1115_PGA_0V256) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->pga = pga;
    return DRIVER_OK;
}

int ads1115_set_data_rate(ads1115_handle_t *handle, ads1115_dr_t data_rate)
{
    if (handle == NULL || !handle->initialized || (uint16_t)data_rate > (uint16_t)ADS1115_DR_860) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->data_rate = data_rate;
    return DRIVER_OK;
}

int ads1115_read_raw(ads1115_handle_t *handle, ads1115_mux_t mux, int16_t *raw)
{
    uint16_t config;
    uint32_t start;
    uint32_t timeout_ms;
    uint8_t buf[2];

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if ((uint16_t)mux > (uint16_t)ADS1115_MUX_SINGLE_3) {
        return (int)HAL_INVALID_PARAM;
    }

    config = ads1115_build_config(handle, mux, true);
    if (ads1115_write_config(handle, config) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    timeout_ms = (uint32_t)ads1115_conv_ms[handle->data_rate] + ADS1115_CONV_EXTRA_MS;
    start = driver_tick_ms();
    for (;;) {
        if (ads1115_read_config(handle, &config) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        if ((config & ADS1115_CFG_OS) != 0U) {
            break;   /* 转换完成，OS 自动置 1 */
        }
        if ((uint32_t)(driver_tick_ms() - start) > timeout_ms) {
            LOG_E("ads1115", "conversion timeout");
            return (int)HAL_TIMEOUT;
        }
        driver_delay_ms(1U);
    }

    if (driver_i2c_read_reg(&handle->i2c, ADS1115_REG_CONVERSION, buf, 2U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    *raw = driver_be16(buf);
    return DRIVER_OK;
}

int ads1115_read_voltage(ads1115_handle_t *handle, uint8_t channel, float *volts)
{
    int16_t raw = 0;
    int ret;

    if (handle == NULL || volts == NULL || !handle->initialized || channel > 3U) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = ads1115_read_raw(handle, (ads1115_mux_t)((uint16_t)ADS1115_MUX_SINGLE_0 + channel), &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *volts = (float)raw * (float)ads1115_fs_mv[handle->pga] / 32768.0f / 1000.0f;
    return DRIVER_OK;
}

int ads1115_read(ads1115_handle_t *handle, int16_t *values, size_t count)
{
    size_t i;

    if (handle == NULL || values == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (count == 0U || count > 4U) {
        return (int)HAL_INVALID_PARAM;
    }

    for (i = 0U; i < count; i++) {
        int ret = ads1115_read_raw(handle,
                                   (ads1115_mux_t)((uint16_t)ADS1115_MUX_SINGLE_0 + i),
                                   &values[i]);
        if (ret != DRIVER_OK) {
            return ret;
        }
    }
    return DRIVER_OK;
}
