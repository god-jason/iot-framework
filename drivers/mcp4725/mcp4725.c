#include "mcp4725.h"
#include "mcp4725_register.h"
#include "log.h"

static int mcp4725_write_frame(mcp4725_handle_t *handle, const uint8_t *buf, size_t len)
{
    if (driver_i2c_write(&handle->i2c, buf, len) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

/* 2 字节快速写：0000 D11..D8 | D7..D0 */
static int mcp4725_fast_write(mcp4725_handle_t *handle, uint16_t value)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)((value >> 8) & 0x0FU);
    buf[1] = (uint8_t)(value & 0xFFU);
    if (mcp4725_write_frame(handle, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    handle->dac_value = (uint16_t)(value & MCP4725_FULL_SCALE);
    return DRIVER_OK;
}

/* 3 字节命令写：CMD | PD | D11..D4 | D3..D0 */
static int mcp4725_cmd_write(mcp4725_handle_t *handle, uint8_t cmd,
                             uint16_t value, mcp4725_pd_t pd)
{
    uint8_t buf[3];

    buf[0] = (uint8_t)(cmd | (uint8_t)(((uint8_t)pd << MCP4725_PD_SHIFT) & MCP4725_PD_MASK));
    buf[1] = (uint8_t)((value >> MCP4725_DATA_SHIFT) & 0xFFU);
    buf[2] = (uint8_t)((value & MCP4725_DATA_LOW_MASK) << MCP4725_DATA_SHIFT);
    if (mcp4725_write_frame(handle, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    handle->dac_value = (uint16_t)(value & MCP4725_FULL_SCALE);
    return DRIVER_OK;
}

int mcp4725_init(mcp4725_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t buf[MCP4725_READ_LEN] = { 0U };
    uint16_t value;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : MCP4725_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->vref_mv = MCP4725_MAX_VREF_MV;
    handle->dac_value = 0U;
    handle->initialized = false;

    if (handle->i2c.address > MCP4725_ADDRESS_MAX) {
        return (int)IOT_INVALID_PARAM;
    }

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("mcp4725", "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    /* 读回 5 字节：状态 + DAC 寄存器 + EEPROM 内容（EEPROM 内容不解析） */
    if (driver_i2c_read(&handle->i2c, buf, MCP4725_READ_LEN) != DRIVER_OK) {
        return (int)IOT_NOT_FOUND;
    }

    /* buf[0]: bit3..0 为 D11..D8，bit5:4 为掉电模式；buf[1] 为 D7..D0 */
    value = (uint16_t)(((uint16_t)(buf[0] & 0x0FU) << 8) | (uint16_t)buf[1]);
    handle->dac_value = (uint16_t)(value & MCP4725_FULL_SCALE);

    handle->initialized = true;
    LOG_I("mcp4725", "init ok at 0x%02X, dac=%u",
          handle->i2c.address, (unsigned)handle->dac_value);
    return DRIVER_OK;
}

int mcp4725_deinit(mcp4725_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int mcp4725_set_vref(mcp4725_handle_t *handle, uint16_t vref_mv)
{
    if (handle == NULL || !handle->initialized || vref_mv == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    handle->vref_mv = vref_mv;
    return DRIVER_OK;
}

int mcp4725_write(mcp4725_handle_t *handle, uint16_t value)
{
    if (handle == NULL || !handle->initialized || value > MCP4725_FULL_SCALE) {
        return (int)IOT_INVALID_PARAM;
    }
    return mcp4725_fast_write(handle, value);
}

int mcp4725_set_raw(mcp4725_handle_t *handle, uint16_t value)
{
    return mcp4725_write(handle, value);
}

int mcp4725_write_dac(mcp4725_handle_t *handle, uint16_t value, mcp4725_pd_t pd)
{
    if (handle == NULL || !handle->initialized || value > MCP4725_FULL_SCALE ||
        (uint16_t)pd > (uint16_t)MCP4725_PD_500K_TO_GND) {
        return (int)IOT_INVALID_PARAM;
    }
    return mcp4725_cmd_write(handle, MCP4725_CMD_WRITE_DAC, value, pd);
}

int mcp4725_save(mcp4725_handle_t *handle, uint16_t value, mcp4725_pd_t pd)
{
    int ret;

    if (handle == NULL || !handle->initialized || value > MCP4725_FULL_SCALE ||
        (uint16_t)pd > (uint16_t)MCP4725_PD_500K_TO_GND) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = mcp4725_cmd_write(handle, MCP4725_CMD_WRITE_DAC_EE, value, pd);
    if (ret != DRIVER_OK) {
        return ret;
    }
    /* EEPROM 写入需要等待，期间器件不响应 */
    driver_delay_ms(MCP4725_EEPROM_WRITE_MS);
    return DRIVER_OK;
}

int mcp4725_set_voltage(mcp4725_handle_t *handle, float volts)
{
    float full_scale;
    float code;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    full_scale = (float)handle->vref_mv / 1000.0f;
    if (volts <= 0.0f) {
        return mcp4725_fast_write(handle, 0U);
    }
    if (volts >= full_scale) {
        return mcp4725_fast_write(handle, (uint16_t)MCP4725_FULL_SCALE);
    }

    code = volts * (float)MCP4725_RESOLUTION / full_scale;
    if (code > (float)MCP4725_FULL_SCALE) {
        code = (float)MCP4725_FULL_SCALE;
    }
    return mcp4725_fast_write(handle, (uint16_t)code);
}

int mcp4725_read(mcp4725_handle_t *handle, uint16_t *value, mcp4725_pd_t *pd)
{
    uint8_t buf[MCP4725_READ_LEN] = { 0U };
    uint16_t dac;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (value == NULL && pd == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    if (driver_i2c_read(&handle->i2c, buf, MCP4725_READ_LEN) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    dac = (uint16_t)(((uint16_t)(buf[0] & 0x0FU) << 8) | (uint16_t)buf[1]);
    if (value != NULL) {
        *value = (uint16_t)(dac & MCP4725_FULL_SCALE);
    }
    if (pd != NULL) {
        *pd = (mcp4725_pd_t)((buf[0] >> 4) & 0x03U);
    }
    return DRIVER_OK;
}
