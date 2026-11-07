#include "tea5767.h"
#include "tea5767_register.h"
#include "log.h"

#define TEA5767_TAG "tea5767"

static int tea5767_check(const tea5767_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

static float tea5767_pll_to_mhz(uint16_t pll)
{
    return (((float)pll * TEA5767_PLL_DIVIDER) - TEA5767_PLL_OFFSET_HZ) / 1000000.0f;
}

static uint16_t tea5767_mhz_to_pll(float mhz)
{
    float hz = mhz * 1000000.0f;

    return (uint16_t)(((hz + TEA5767_PLL_OFFSET_HZ) / TEA5767_PLL_DIVIDER) + 0.5f);
}

int tea5767_init(tea5767_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t i;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : TEA5767_ADDRESS_DEFAULT;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    for (i = 0U; i < TEA5767_REG_COUNT; i++) {
        handle->config[i] = 0x00U;
    }
    /* 默认：欧洲/美国频段、立体声、非静音、非待机、50us 去加重 */
    handle->config[2] = 0x00U;
    handle->config[3] = 0x00U;
    handle->config[4] = 0x00U;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E(TEA5767_TAG, "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    handle->initialized = true;
    if (tea5767_set_frequency(handle, 98.0f) != DRIVER_OK) {
        LOG_W(TEA5767_TAG, "default frequency write failed");
    }

    LOG_I(TEA5767_TAG, "init ok at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int tea5767_deinit(tea5767_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)tea5767_set_standby(handle, true);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int tea5767_write(tea5767_handle_t *handle, const uint8_t config[TEA5767_REG_COUNT])
{
    uint8_t i;
    int ret;

    ret = tea5767_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (config == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = driver_i2c_write(&handle->i2c, config, TEA5767_REG_COUNT);
    if (ret != DRIVER_OK) {
        return ret;
    }
    for (i = 0U; i < TEA5767_REG_COUNT; i++) {
        handle->config[i] = config[i];
    }
    return DRIVER_OK;
}

int tea5767_read(tea5767_handle_t *handle, tea5767_status_t *status)
{
    uint8_t buf[TEA5767_REG_COUNT];
    uint16_t pll;
    int ret;

    if (status == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = tea5767_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    ret = driver_i2c_read(&handle->i2c, buf, TEA5767_REG_COUNT);
    if (ret != DRIVER_OK) {
        return ret;
    }

    status->ready = ((buf[0] & TEA5767_R0_READY) != 0U);
    status->band_japan = ((buf[0] & TEA5767_R0_BAND) != 0U);
    status->stereo = ((buf[2] & TEA5767_R2_STEREO) != 0U);

    pll = (uint16_t)(((uint16_t)(buf[0] & TEA5767_B0_PLL_HI_MASK) << 8) | buf[1]);
    status->pll = pll;
    status->frequency_mhz = tea5767_pll_to_mhz(pll);
    /* IF 计数器位于字节 2 的低 7 位与字节 3 的高位，此处按 datasheet 顺序近似还原 */
    status->if_count = (uint8_t)(((buf[2] & 0x7FU) >> 1));
    /* 信号电平 ADC 位于字节 4 的高 4 位 */
    status->signal_level = (uint8_t)((buf[4] >> 4) & 0x0FU);
    return DRIVER_OK;
}

int tea5767_set_frequency(tea5767_handle_t *handle, float frequency_mhz)
{
    uint16_t pll;
    uint8_t config[TEA5767_REG_COUNT];
    bool japan;
    uint8_t i;
    int ret;

    ret = tea5767_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    japan = ((handle->config[2] & TEA5767_B2_HLSI) != 0U);
    if (japan) {
        if (frequency_mhz < TEA5767_FREQ_JAPAN_MIN_MHZ ||
            frequency_mhz > TEA5767_FREQ_JAPAN_MAX_MHZ) {
            return (int)IOT_INVALID_PARAM;
        }
    } else if (frequency_mhz < TEA5767_FREQ_MIN_MHZ ||
               frequency_mhz > TEA5767_FREQ_MAX_MHZ) {
        return (int)IOT_INVALID_PARAM;
    }

    pll = tea5767_mhz_to_pll(frequency_mhz);

    for (i = 0U; i < TEA5767_REG_COUNT; i++) {
        config[i] = handle->config[i];
    }
    config[0] = (uint8_t)((config[0] & TEA5767_B0_MUTE) |
                          ((pll >> 8) & TEA5767_B0_PLL_HI_MASK));
    config[1] = (uint8_t)(pll & TEA5767_B1_PLL_MASK);
    /* 频率写入时清除搜索模式位 */
    config[0] = (uint8_t)(config[0] & (uint8_t)~TEA5767_B0_SEARCH_MODE);

    return tea5767_write(handle, config);
}

int tea5767_get_frequency(tea5767_handle_t *handle, float *frequency_mhz)
{
    tea5767_status_t status;
    int ret;

    if (frequency_mhz == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = tea5767_read(handle, &status);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *frequency_mhz = status.frequency_mhz;
    return DRIVER_OK;
}

/* 修改缓存配置中的某一位后整体下发 */
static int tea5767_modify_bit(tea5767_handle_t *handle, uint8_t index, uint8_t mask, bool set)
{
    uint8_t config[TEA5767_REG_COUNT];
    uint8_t i;
    int ret = tea5767_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    for (i = 0U; i < TEA5767_REG_COUNT; i++) {
        config[i] = handle->config[i];
    }
    if (set) {
        config[index] |= mask;
    } else {
        config[index] = (uint8_t)(config[index] & (uint8_t)~mask);
    }
    return tea5767_write(handle, config);
}

int tea5767_set_mute(tea5767_handle_t *handle, bool mute)
{
    return tea5767_modify_bit(handle, 0U, TEA5767_B0_MUTE, mute);
}

int tea5767_set_stereo(tea5767_handle_t *handle, bool enable)
{
    /* MONO 位为 1 表示强制单声道 */
    return tea5767_modify_bit(handle, 2U, TEA5767_B2_MONO, !enable);
}

int tea5767_set_standby(tea5767_handle_t *handle, bool standby)
{
    return tea5767_modify_bit(handle, 3U, TEA5767_B3_STANDBY, standby);
}

int tea5767_seek(tea5767_handle_t *handle, bool up, float *mhz_out)
{
    uint8_t config[TEA5767_REG_COUNT];
    tea5767_status_t status;
    uint32_t start;
    uint8_t i;
    int ret;

    ret = tea5767_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (mhz_out == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    for (i = 0U; i < TEA5767_REG_COUNT; i++) {
        config[i] = handle->config[i];
    }
    config[0] |= TEA5767_B0_SEARCH_MODE;
    if (up) {
        config[2] |= TEA5767_B2_SEARCH_UP;
    } else {
        config[2] = (uint8_t)(config[2] & (uint8_t)~TEA5767_B2_SEARCH_UP);
    }
    ret = tea5767_write(handle, config);
    if (ret != DRIVER_OK) {
        return ret;
    }

    start = driver_tick_ms();
    for (;;) {
        driver_delay_ms(TEA5767_SEARCH_POLL_MS);
        ret = tea5767_read(handle, &status);
        if (ret != DRIVER_OK) {
            return ret;
        }
        if (status.ready) {
            break;
        }
        if ((uint32_t)(driver_tick_ms() - start) > TEA5767_SEARCH_TIMEOUT_MS) {
            LOG_W(TEA5767_TAG, "seek timeout");
            return (int)IOT_TIMEOUT;
        }
    }

    /* 退出搜索模式，锁定当前 PLL */
    config[0] = (uint8_t)(config[0] & (uint8_t)~TEA5767_B0_SEARCH_MODE);
    ret = tea5767_write(handle, config);
    if (ret != DRIVER_OK) {
        return ret;
    }

    *mhz_out = status.frequency_mhz;
    return DRIVER_OK;
}
