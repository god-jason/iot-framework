#include "wm8978.h"
#include "log.h"

#define WM8978_TAG "wm8978"
#define WM8978_TRY(expr)            \
    do {                            \
        int r_ = (expr);            \
        if (r_ != DRIVER_OK) {      \
            return r_;              \
        }                           \
    } while (0)

static int wm8978_check(const wm8978_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

static bool wm8978_reg_valid(uint8_t reg)
{
    return (reg < WM8978_REG_COUNT);
}

int wm8978_write(wm8978_handle_t *handle, uint8_t reg, uint16_t value)
{
    uint8_t frame[2];
    uint16_t word;
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (!wm8978_reg_valid(reg) || (value & 0xFE00U) != 0U) {
        return (int)IOT_INVALID_PARAM; /* 地址 7 位、数据 9 位 */
    }

    word = (uint16_t)(((uint16_t)(reg & 0x7FU) << 9) | (value & 0x01FFU));
    frame[0] = (uint8_t)(word >> 8);
    frame[1] = (uint8_t)(word & 0xFFU);

    ret = driver_i2c_write(&handle->i2c, frame, sizeof(frame));
    if (ret != DRIVER_OK) {
        return ret;
    }
    handle->shadow[reg] = (uint16_t)(value & 0x01FFU);
    driver_delay_ms(WM8978_CONFIG_DELAY_MS);
    return DRIVER_OK;
}

int wm8978_read(wm8978_handle_t *handle, uint8_t reg, uint16_t *value)
{
    int ret;

    if (value == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = wm8978_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (!wm8978_reg_valid(reg)) {
        return (int)IOT_INVALID_PARAM;
    }

    /* 控制口写-only，返回影子寄存器（上电后未写过的寄存器读到 0） */
    *value = handle->shadow[reg];
    return DRIVER_OK;
}

int wm8978_init(wm8978_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t reg;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : WM8978_ADDRESS_DEFAULT;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->playing = false;
    handle->initialized = false;

    for (reg = 0U; reg < WM8978_REG_COUNT; reg++) {
        handle->shadow[reg] = 0U;
    }

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E(WM8978_TAG, "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    handle->initialized = true;
    driver_delay_ms(WM8978_POWER_UP_MS);

    /* 上电默认：关闭输出级与 DAC */
    ret = wm8978_write(handle, WM8978_REG_PM3, 0x0000U);
    if (ret == DRIVER_OK) {
        ret = wm8978_write(handle, WM8978_REG_PM2, 0x0000U);
    }
    if (ret == DRIVER_OK) {
        ret = wm8978_write(handle, WM8978_REG_PM1, WM8978_DEF_PM1);
    }
    if (ret == DRIVER_OK) {
        ret = wm8978_write(handle, WM8978_REG_AUDIO_IFACE, WM8978_DEF_IFACE);
    }
    if (ret != DRIVER_OK) {
        handle->initialized = false;
        return ret;
    }

    LOG_I(WM8978_TAG, "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int wm8978_deinit(wm8978_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)wm8978_play_stop(handle);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int wm8978_play_start(wm8978_handle_t *handle)
{
    int ret = wm8978_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* DAC 接入 L/R 输出混音器（取值需按 datasheet/固件确认） */
    WM8978_TRY(wm8978_write(handle, WM8978_REG_LOUTMIX, WM8978_MIX_DAC_TO_OUT));
    WM8978_TRY(wm8978_write(handle, WM8978_REG_ROUTMIX, WM8978_MIX_DAC_TO_OUT));

    /* 输出音量 0dB、取消静音 */
    WM8978_TRY(wm8978_write(handle, WM8978_REG_LOUT1, WM8978_DEF_LOUT1));
    WM8978_TRY(wm8978_write(handle, WM8978_REG_ROUT1, WM8978_DEF_ROUT1));
    WM8978_TRY(wm8978_write(handle, WM8978_REG_LOUT2, WM8978_DEF_LOUT2));
    WM8978_TRY(wm8978_write(handle, WM8978_REG_ROUT2, WM8978_DEF_ROUT2));

    /* DAC 数字音量 */
    WM8978_TRY(wm8978_write(handle, WM8978_REG_DAC_VOL_L, WM8978_DEF_DAC_VOL));
    WM8978_TRY(wm8978_write(handle, WM8978_REG_DAC_VOL_R, WM8978_DEF_DAC_VOL));

    /* 先开 DAC，再开输出级 */
    WM8978_TRY(wm8978_write(handle, WM8978_REG_PM2,
                            (uint16_t)(WM8978_PM2_DACL | WM8978_PM2_DACR)));
    WM8978_TRY(wm8978_write(handle, WM8978_REG_PM3,
                            (uint16_t)(WM8978_PM3_LOUT1 | WM8978_PM3_ROUT1 |
                                       WM8978_PM3_LOUT2 | WM8978_PM3_ROUT2)));

    handle->playing = true;
    return DRIVER_OK;
}

int wm8978_play_stop(wm8978_handle_t *handle)
{
    int ret = wm8978_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 先关输出级，再关 DAC */
    WM8978_TRY(wm8978_write(handle, WM8978_REG_PM3, 0x0000U));
    WM8978_TRY(wm8978_write(handle, WM8978_REG_PM2, 0x0000U));
    handle->playing = false;
    return DRIVER_OK;
}

int wm8978_set_volume(wm8978_handle_t *handle, uint8_t volume)
{
    uint16_t value;
    int ret = wm8978_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (volume > WM8978_VOL_MASK) {
        return (int)IOT_INVALID_PARAM;
    }

    value = (uint16_t)(WM8978_VOL_UPDATE | (uint16_t)(volume & WM8978_VOL_MASK));
    WM8978_TRY(wm8978_write(handle, WM8978_REG_LOUT1, value));
    WM8978_TRY(wm8978_write(handle, WM8978_REG_ROUT1, value));
    WM8978_TRY(wm8978_write(handle, WM8978_REG_LOUT2, value));
    return wm8978_write(handle, WM8978_REG_ROUT2, value);
}

int wm8978_set_mute(wm8978_handle_t *handle, bool mute)
{
    static const uint8_t regs[4] = {
        WM8978_REG_LOUT1, WM8978_REG_ROUT1, WM8978_REG_LOUT2, WM8978_REG_ROUT2
    };
    uint8_t i;
    int ret;

    ret = wm8978_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    for (i = 0U; i < (uint8_t)DRIVER_ARRAY_SIZE(regs); i++) {
        uint16_t value = handle->shadow[regs[i]];

        if (mute) {
            value |= WM8978_VOL_MUTE;
        } else {
            value = (uint16_t)(value & (uint16_t)~WM8978_VOL_MUTE);
        }
        value |= WM8978_VOL_UPDATE;
        ret = wm8978_write(handle, regs[i], value);
        if (ret != DRIVER_OK) {
            return ret;
        }
    }
    return DRIVER_OK;
}

int wm8978_set_speaker(wm8978_handle_t *handle, bool enable)
{
    uint16_t pm3;
    int ret = wm8978_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    pm3 = handle->shadow[WM8978_REG_PM3];
    if (enable) {
        pm3 |= (uint16_t)(WM8978_PM3_LOUT2 | WM8978_PM3_ROUT2 | WM8978_PM3_SPK);
        pm3 = (uint16_t)(pm3 & (uint16_t)~(WM8978_PM3_LOUT1 | WM8978_PM3_ROUT1));
    } else {
        pm3 |= (uint16_t)(WM8978_PM3_LOUT1 | WM8978_PM3_ROUT1);
        pm3 = (uint16_t)(pm3 & (uint16_t)~(WM8978_PM3_LOUT2 | WM8978_PM3_ROUT2 |
                                           WM8978_PM3_SPK));
    }
    return wm8978_write(handle, WM8978_REG_PM3, pm3);
}

int wm8978_set_interface(wm8978_handle_t *handle, bool i2s, uint8_t word_length)
{
    static const struct {
        uint8_t bits;
        uint16_t code;
    } table[4] = {
        { 16U, WM8978_IFACE_WL_16BIT },
        { 20U, WM8978_IFACE_WL_20BIT },
        { 24U, WM8978_IFACE_WL_24BIT },
        { 32U, WM8978_IFACE_WL_32BIT }
    };
    uint16_t value;
    uint8_t i;
    int ret = wm8978_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }

    value = i2s ? WM8978_IFACE_FMT_I2S : WM8978_IFACE_FMT_LEFT_JUST;
    for (i = 0U; i < (uint8_t)DRIVER_ARRAY_SIZE(table); i++) {
        if (table[i].bits == word_length) {
            value |= table[i].code;
            return wm8978_write(handle, WM8978_REG_AUDIO_IFACE, value);
        }
    }
    return (int)IOT_INVALID_PARAM;
}

int wm8978_set_sample_rate(wm8978_handle_t *handle, uint32_t sample_rate)
{
    static const struct {
        uint32_t rate;
        uint16_t code;
    } table[8] = {
        { 8000U, WM8978_SR_8K },   { 12000U, WM8978_SR_12K },
        { 16000U, WM8978_SR_16K }, { 24000U, WM8978_SR_24K },
        { 32000U, WM8978_SR_32K }, { 44100U, WM8978_SR_44K1 },
        { 48000U, WM8978_SR_48K }, { 96000U, WM8978_SR_96K }
    };
    uint8_t i;
    int ret = wm8978_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 从机模式：时钟由外部 MCLK/BCLK 提供，这里只配置分频寄存器 */
    for (i = 0U; i < (uint8_t)DRIVER_ARRAY_SIZE(table); i++) {
        if (table[i].rate == sample_rate) {
            return wm8978_write(handle, WM8978_REG_CLOCKING, table[i].code);
        }
    }
    return (int)IOT_INVALID_PARAM;
}
