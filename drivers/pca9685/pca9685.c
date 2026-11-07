#include "pca9685.h"
#include "pca9685_register.h"
#include "log.h"

static int pca9685_write_reg(pca9685_handle_t *handle, uint8_t reg, uint8_t value)
{
    return (driver_i2c_write_reg(&handle->i2c, reg, &value, 1U) == DRIVER_OK)
               ? DRIVER_OK : (int)IOT_IO_ERROR;
}

static int pca9685_read_reg(pca9685_handle_t *handle, uint8_t reg, uint8_t *value)
{
    return (driver_i2c_read_reg(&handle->i2c, reg, value, 1U) == DRIVER_OK)
               ? DRIVER_OK : (int)IOT_IO_ERROR;
}

static uint8_t pca9685_channel_reg(uint8_t channel)
{
    return (uint8_t)(PCA9685_REG_LED0_ON_L + 4U * channel);
}

/* ON/OFF 计数值打包为 4 字节，配合地址自动递增写入 */
static void pca9685_pack(uint8_t *buf, uint16_t on, uint16_t off)
{
    buf[0] = (uint8_t)(on & 0xFFU);
    buf[1] = (uint8_t)((on >> 8) & PCA9685_CNT_MASK);
    buf[2] = (uint8_t)(off & 0xFFU);
    buf[3] = (uint8_t)((off >> 8) & PCA9685_CNT_MASK);
}

int pca9685_init(pca9685_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t mode1 = 0U;
    uint8_t prescale = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : PCA9685_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->frequency = PCA9685_DEFAULT_FREQ_HZ;
    handle->initialized = false;

    if (handle->i2c.address > PCA9685_ADDRESS_MAX) {
        return (int)IOT_INVALID_PARAM;
    }

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("pca9685", "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    /* 唤醒并打开地址自动递增，清除 RESTART */
    if (pca9685_write_reg(handle, PCA9685_REG_MODE1, PCA9685_MODE1_DEFAULT) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(PCA9685_WAKE_DELAY_MS);

    if (pca9685_read_reg(handle, PCA9685_REG_MODE1, &mode1) != DRIVER_OK) {
        return (int)IOT_NOT_FOUND;
    }
    if ((mode1 & PCA9685_MODE1_AI) == 0U) {
        LOG_E("pca9685", "MODE1 readback 0x%02X invalid", (unsigned)mode1);
        return (int)IOT_NOT_FOUND;
    }

    /* 推挽输出（如需开漏驱动 LED 可改为 MODE2_OUTDRV=0） */
    if (pca9685_write_reg(handle, PCA9685_REG_MODE2, PCA9685_MODE2_DEFAULT) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    /* 按实际 prescale 换算当前频率 */
    if (pca9685_read_reg(handle, PCA9685_REG_PRESCALE, &prescale) == DRIVER_OK) {
        handle->frequency = (float)PCA9685_OSC_HZ /
                            ((float)PCA9685_RESOLUTION * (float)(prescale + 1U));
    }

    handle->initialized = true;
    LOG_I("pca9685", "init ok at 0x%02X, %.1f Hz",
          handle->i2c.address, (double)handle->frequency);
    return DRIVER_OK;
}

int pca9685_deinit(pca9685_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)pca9685_write_reg(handle, PCA9685_REG_ALL_LED_OFF_H, PCA9685_FULL_BIT);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int pca9685_set_pwm_freq(pca9685_handle_t *handle, float freq_hz)
{
    uint8_t mode1 = 0U;
    uint8_t prescale;
    uint32_t scaler;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (freq_hz < 24.0f || freq_hz > 1526.0f) {
        return (int)IOT_INVALID_PARAM;
    }

    scaler = (uint32_t)(((float)PCA9685_OSC_HZ /
                         ((float)PCA9685_RESOLUTION * freq_hz)) + 0.5f);
    if (scaler < (uint32_t)PCA9685_PRESCALE_MIN) {
        scaler = (uint32_t)PCA9685_PRESCALE_MIN;
    }
    if (scaler > (uint32_t)PCA9685_PRESCALE_MAX) {
        scaler = (uint32_t)PCA9685_PRESCALE_MAX;
    }
    prescale = (uint8_t)(scaler - 1U);

    if (pca9685_read_reg(handle, PCA9685_REG_MODE1, &mode1) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    /* PRESCALE 只能在 SLEEP 状态下写入 */
    if (pca9685_write_reg(handle, PCA9685_REG_MODE1,
                          (uint8_t)((mode1 & (uint8_t)~PCA9685_MODE1_RESTART) |
                                    PCA9685_MODE1_SLEEP)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(PCA9685_SLEEP_DELAY_MS);

    if (pca9685_write_reg(handle, PCA9685_REG_PRESCALE, prescale) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    if (pca9685_write_reg(handle, PCA9685_REG_MODE1,
                          (uint8_t)(mode1 & (uint8_t)~PCA9685_MODE1_SLEEP)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    driver_delay_ms(PCA9685_WAKE_DELAY_MS);

    handle->frequency = (float)PCA9685_OSC_HZ /
                        ((float)PCA9685_RESOLUTION * (float)(prescale + 1U));
    return DRIVER_OK;
}

int pca9685_set_pwm(pca9685_handle_t *handle, uint8_t channel, uint16_t on, uint16_t off)
{
    uint8_t buf[4];

    if (handle == NULL || !handle->initialized || channel >= PCA9685_CHANNEL_COUNT) {
        return (int)IOT_INVALID_PARAM;
    }
    if (on > PCA9685_FULL_SCALE || off > PCA9685_FULL_SCALE) {
        return (int)IOT_INVALID_PARAM;
    }

    pca9685_pack(buf, on, off);
    if (driver_i2c_write_reg(&handle->i2c, pca9685_channel_reg(channel), buf, 4U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

int pca9685_set_full_on(pca9685_handle_t *handle, uint8_t channel, bool full_on)
{
    uint8_t reg;
    uint8_t value;

    if (handle == NULL || !handle->initialized || channel >= PCA9685_CHANNEL_COUNT) {
        return (int)IOT_INVALID_PARAM;
    }

    reg = (uint8_t)(pca9685_channel_reg(channel) + 3U);   /* LEDn_OFF_H */
    value = full_on ? (uint8_t)PCA9685_FULL_BIT : 0x00U;
    return pca9685_write_reg(handle, reg, value);
}

int pca9685_set_duty(pca9685_handle_t *handle, uint8_t channel, float duty)
{
    float code;

    if (handle == NULL || !handle->initialized || channel >= PCA9685_CHANNEL_COUNT) {
        return (int)IOT_INVALID_PARAM;
    }
    if (duty <= 0.0f) {
        return pca9685_set_pwm(handle, channel, 0U, 0U);
    }
    if (duty >= 1.0f) {
        int ret = pca9685_set_pwm(handle, channel, 0U, 0U);

        if (ret != DRIVER_OK) {
            return ret;
        }
        return pca9685_set_full_on(handle, channel, true);
    }

    code = duty * (float)PCA9685_RESOLUTION;
    if (code > (float)PCA9685_FULL_SCALE) {
        code = (float)PCA9685_FULL_SCALE;
    }
    return pca9685_set_pwm(handle, channel, 0U, (uint16_t)code);
}

int pca9685_set_all(pca9685_handle_t *handle, uint16_t on, uint16_t off)
{
    uint8_t buf[4];

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (on > PCA9685_FULL_SCALE || off > PCA9685_FULL_SCALE) {
        return (int)IOT_INVALID_PARAM;
    }

    pca9685_pack(buf, on, off);
    if (driver_i2c_write_reg(&handle->i2c, PCA9685_REG_ALL_LED_ON_L, buf, 4U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

int pca9685_enable_output(pca9685_handle_t *handle, bool enable)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    /* 全关位：1 时所有通道输出恒低 */
    return pca9685_write_reg(handle, PCA9685_REG_ALL_LED_OFF_H,
                             enable ? 0x00U : (uint8_t)PCA9685_FULL_BIT);
}


