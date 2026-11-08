#include "mcp3421.h"
#include "mcp3421_register.h"
#include "log.h"

/* 分辨率枚举 → 位数 */
static uint8_t mcp3421_bits(mcp3421_res_t resolution)
{
    return (uint8_t)(12U + 2U * ((uint8_t)resolution & 0x03U));
}

/* 分辨率枚举 + PGA → 总增益（S 的 bit2 为内建 ×2 档） */
static uint8_t mcp3421_total_gain(const mcp3421_handle_t *handle)
{
    uint8_t base = (((uint8_t)handle->resolution & 0x04U) != 0U) ? 2U : 1U;

    return (uint8_t)(base << (uint8_t)handle->pga);
}

static uint8_t mcp3421_config(const mcp3421_handle_t *handle)
{
    uint8_t config;

    config = (uint8_t)(((uint8_t)handle->mode << MCP3421_CFG_MODE_SHIFT) & MCP3421_CFG_MODE_MASK);
    config |= (uint8_t)(((uint8_t)handle->resolution << MCP3421_CFG_S_SHIFT) & MCP3421_CFG_S_MASK);
    config |= (uint8_t)(((uint8_t)handle->pga << MCP3421_CFG_G_SHIFT) & MCP3421_CFG_G_MASK);
    return config;
}

static int mcp3421_write_config(mcp3421_handle_t *handle, uint8_t config)
{
    if (driver_i2c_write(&handle->i2c, &config, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

/* 读一帧：数据字节 + 配置回读字节 */
static int mcp3421_read_frame(mcp3421_handle_t *handle, uint8_t *buf, size_t len)
{
    if (driver_i2c_read(&handle->i2c, buf, len) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

static uint32_t mcp3421_conv_ms(mcp3421_res_t resolution)
{
    switch (mcp3421_bits(resolution)) {
    case 18U:
        return MCP3421_CONV_MS_18BIT;
    case 16U:
        return MCP3421_CONV_MS_16BIT;
    case 14U:
        return MCP3421_CONV_MS_14BIT;
    default:
        return MCP3421_CONV_MS_12BIT;
    }
}

int mcp3421_init(mcp3421_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t buf[MCP3421_FRAME_MAX_LEN] = { 0U, 0U, 0U };
    uint8_t config;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : MCP3421_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->resolution = MCP3421_RES_18BIT_1X;
    handle->pga = MCP3421_GAIN_1;
    handle->mode = MCP3421_MODE_ONESHOT;
    handle->initialized = false;

    if (handle->i2c.address != MCP3421_DEFAULT_ADDRESS) {
        return (int)HAL_INVALID_PARAM;   /* 地址由出厂固化 */
    }

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("mcp3421", "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    config = mcp3421_config(handle);
    if (mcp3421_write_config(handle, config) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(MCP3421_CONV_MS_18BIT + MCP3421_CONV_EXTRA_MS);

    /* 回读配置（第 3 字节），校验可写位是否与写入一致 */
    if (mcp3421_read_frame(handle, buf, MCP3421_FRAME_MAX_LEN) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if ((buf[2] & MCP3421_CFG_FIELDS_MASK) != (config & MCP3421_CFG_FIELDS_MASK)) {
        LOG_E("mcp3421", "config readback 0x%02X != 0x%02X", (unsigned)buf[2], (unsigned)config);
        return (int)HAL_NOT_FOUND;
    }

    handle->initialized = true;
    LOG_I("mcp3421", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int mcp3421_deinit(mcp3421_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int mcp3421_set_resolution(mcp3421_handle_t *handle, mcp3421_res_t resolution)
{
    if (handle == NULL || !handle->initialized ||
        (uint16_t)resolution > (uint16_t)MCP3421_RES_18BIT_2X) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->resolution = resolution;
    return mcp3421_write_config(handle, mcp3421_config(handle));
}

int mcp3421_set_gain(mcp3421_handle_t *handle, mcp3421_gain_t pga)
{
    if (handle == NULL || !handle->initialized || (uint16_t)pga > (uint16_t)MCP3421_GAIN_8) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->pga = pga;
    return mcp3421_write_config(handle, mcp3421_config(handle));
}

int mcp3421_set_mode(mcp3421_handle_t *handle, mcp3421_mode_t mode)
{
    if (handle == NULL || !handle->initialized ||
        (uint16_t)mode > (uint16_t)MCP3421_MODE_CONTINUOUS) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->mode = mode;
    return mcp3421_write_config(handle, mcp3421_config(handle));
}

int mcp3421_read_raw(mcp3421_handle_t *handle, int32_t *raw)
{
    uint8_t buf[MCP3421_FRAME_MAX_LEN] = { 0U, 0U, 0U };
    uint8_t bits;
    size_t len;
    int32_t value = 0;
    size_t i;

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    bits = mcp3421_bits(handle->resolution);
    len = (bits == 18U) ? 3U : 2U;

    /* 单次模式需重新触发一次转换 */
    if (handle->mode == MCP3421_MODE_ONESHOT) {
        if (mcp3421_write_config(handle, mcp3421_config(handle)) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
    }
    driver_delay_ms(mcp3421_conv_ms(handle->resolution) + MCP3421_CONV_EXTRA_MS);

    if (mcp3421_read_frame(handle, buf, len) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    for (i = 0U; i < len; i++) {
        value = (value << 8) | (int32_t)buf[i];
    }
    /* 数据左对齐，去掉尾部填充位 */
    value >>= (int32_t)(len * 8U - bits);
    /* 符号扩展 */
    if ((value & ((int32_t)1 << (bits - 1U))) != 0) {
        value -= (int32_t)1 << bits;
    }
    *raw = value;
    return DRIVER_OK;
}

int mcp3421_read_voltage(mcp3421_handle_t *handle, float *volts)
{
    int32_t raw = 0;
    uint8_t bits;
    uint8_t gain;
    int ret;

    if (handle == NULL || volts == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = mcp3421_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    bits = mcp3421_bits(handle->resolution);
    gain = mcp3421_total_gain(handle);
    *volts = (float)raw * (float)MCP3421_FS_MV / (float)((int32_t)1 << bits)
             / (float)gain / 1000.0f;
    return DRIVER_OK;
}
