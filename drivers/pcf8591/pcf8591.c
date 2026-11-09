#include "pcf8591.h"
#include "pcf8591_register.h"
#include "log.h"

static uint8_t pcf8591_control(const pcf8591_handle_t *handle)
{
    uint8_t ctrl;

    ctrl = (uint8_t)(((uint8_t)handle->input_mode << PCF8591_CTRL_MODE_SHIFT) &
                     PCF8591_CTRL_MODE_MASK);
    ctrl |= (uint8_t)((handle->channel << PCF8591_CTRL_CH_SHIFT) & PCF8591_CTRL_CH_MASK);
    if (handle->auto_increment) {
        ctrl |= PCF8591_CTRL_AUTO_INC;
    }
    if (handle->dac_enabled) {
        ctrl |= PCF8591_CTRL_ANALOG_OUT;
    }
    return ctrl;
}

static int pcf8591_write_control(pcf8591_handle_t *handle)
{
    uint8_t ctrl = pcf8591_control(handle);

    if (driver_i2c_write(&handle->i2c, &ctrl, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

int pcf8591_init(pcf8591_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t dummy = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : PCF8591_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->input_mode = PCF8591_MODE_4_SINGLE_ENDED;
    handle->channel = 0U;
    handle->auto_increment = false;
    handle->dac_enabled = false;
    handle->vref_mv = PCF8591_DEFAULT_VREF_MV;
    handle->initialized = false;

    if (handle->i2c.address > PCF8591_ADDRESS_MAX) {
        return (int)HAL_INVALID_PARAM;
    }

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("pcf8591", "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    handle->initialized = true;
    if (pcf8591_write_control(handle) != DRIVER_OK) {
        handle->initialized = false;
        return (int)HAL_IO_ERROR;
    }
    /* 进行一次空读，确认总线可读 */
    if (driver_i2c_read(&handle->i2c, &dummy, 1U) != DRIVER_OK) {
        handle->initialized = false;
        return (int)HAL_IO_ERROR;
    }

    LOG_I("pcf8591", "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int pcf8591_deinit(pcf8591_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int pcf8591_set_input_mode(pcf8591_handle_t *handle, pcf8591_input_mode_e mode)
{
    if (handle == NULL || !handle->initialized ||
        (uint16_t)mode > (uint16_t)PCF8591_MODE_2_DIFFERENTIAL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->input_mode = mode;
    return pcf8591_write_control(handle);
}

int pcf8591_set_channel(pcf8591_handle_t *handle, uint8_t channel)
{
    if (handle == NULL || !handle->initialized || channel >= PCF8591_CHANNEL_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->channel = channel;
    return pcf8591_write_control(handle);
}

int pcf8591_set_auto_increment(pcf8591_handle_t *handle, bool enable)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->auto_increment = enable;
    return pcf8591_write_control(handle);
}

int pcf8591_set_dac_enabled(pcf8591_handle_t *handle, bool enable)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->dac_enabled = enable;
    return pcf8591_write_control(handle);
}

int pcf8591_set_vref(pcf8591_handle_t *handle, uint16_t vref_mv)
{
    if (handle == NULL || !handle->initialized || vref_mv == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->vref_mv = vref_mv;
    return DRIVER_OK;
}

int pcf8591_read(pcf8591_handle_t *handle, uint8_t channel, uint8_t *value)
{
    uint8_t buf[2] = { 0U, 0U };
    int ret;

    if (handle == NULL || value == NULL || !handle->initialized ||
        channel >= PCF8591_CHANNEL_COUNT) {
        return (int)HAL_INVALID_PARAM;
    }

    /* 先写控制字节选择通道 */
    handle->channel = channel;
    ret = pcf8591_write_control(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(PCF8591_CONV_DELAY_MS);

    /*
     * 流水线结构：写控制字节后设备开始转换所选通道，
     * 第一次读出的是上一次的结果（丢弃），第二次读出才是本次转换结果。
     */
    if (driver_i2c_read(&handle->i2c, &buf[0], 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_ms(PCF8591_CONV_DELAY_MS);
    if (driver_i2c_read(&handle->i2c, &buf[1], 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    *value = buf[1];
    return DRIVER_OK;
}

int pcf8591_read_voltage(pcf8591_handle_t *handle, uint8_t channel, float *volts)
{
    uint8_t value = 0U;
    int ret;

    if (handle == NULL || volts == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = pcf8591_read(handle, channel, &value);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *volts = (float)value * (float)handle->vref_mv /
             (float)PCF8591_RESOLUTION / 1000.0f;
    return DRIVER_OK;
}

int pcf8591_write(pcf8591_handle_t *handle, uint8_t value)
{
    uint8_t buf[2];

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    /* 写 DAC：控制字节（使能 AOUT） + 数据字节 */
    handle->dac_enabled = true;
    buf[0] = pcf8591_control(handle);
    buf[1] = value;

    if (driver_i2c_write(&handle->i2c, buf, 2U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}
