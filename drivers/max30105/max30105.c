#include "max30105.h"
#include "max30105_register.h"
#include "log.h"

#define MAX30105_TAG "max30105"
#define MAX30105_TRY(expr)          \
    do {                            \
        int r_ = (expr);            \
        if (r_ != DRIVER_OK) {      \
            return r_;              \
        }                           \
    } while (0)

static int max30105_check(const max30105_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int max30105_write(max30105_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len)
{
    if (handle == NULL || handle->i2c.handle == NULL || data == NULL || len == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    return driver_i2c_write_reg(&handle->i2c, reg, data, len);
}

int max30105_read_reg(max30105_handle_t *handle, uint8_t reg, uint8_t *data, size_t len)
{
    if (handle == NULL || handle->i2c.handle == NULL || data == NULL || len == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, reg, data, len);
}

static int max30105_write_u8(max30105_handle_t *handle, uint8_t reg, uint8_t value)
{
    return max30105_write(handle, reg, &value, 1U);
}

int max30105_clear_fifo(max30105_handle_t *handle)
{
    int ret = max30105_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    MAX30105_TRY(max30105_write_u8(handle, MAX30105_REG_FIFO_WR_PTR, 0x00U));
    MAX30105_TRY(max30105_write_u8(handle, MAX30105_REG_OVF_COUNTER, 0x00U));
    return max30105_write_u8(handle, MAX30105_REG_FIFO_RD_PTR, 0x00U);
}

int max30105_reset(max30105_handle_t *handle)
{
    uint8_t mode = 0U;
    uint32_t start;
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = max30105_write_u8(handle, MAX30105_REG_MODE_CONFIG, MAX30105_MODE_RESET);
    if (ret != DRIVER_OK) {
        return ret;
    }

    start = driver_tick_ms();
    for (;;) {
        ret = max30105_read_reg(handle, MAX30105_REG_MODE_CONFIG, &mode, 1U);
        if (ret != DRIVER_OK) {
            return ret;
        }
        if ((mode & MAX30105_MODE_RESET) == 0U) {
            return DRIVER_OK;
        }
        if ((uint32_t)(driver_tick_ms() - start) > 50U) {
            return (int)IOT_TIMEOUT;
        }
        driver_delay_ms(1U);
    }
}

int max30105_set_mode(max30105_handle_t *handle, max30105_mode_t mode)
{
    int ret = max30105_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (mode != MAX30105_MODE_HR_ONLY && mode != MAX30105_MODE_SPO2 &&
        mode != MAX30105_MODE_MULTI_LED) {
        return (int)IOT_INVALID_PARAM;
    }

    ret = max30105_write_u8(handle, MAX30105_REG_MODE_CONFIG, (uint8_t)mode);
    if (ret != DRIVER_OK) {
        return ret;
    }
    handle->mode = mode;
    handle->channels = (mode == MAX30105_MODE_HR_ONLY) ? 1U
                     : (mode == MAX30105_MODE_SPO2) ? 2U
                                                    : 3U;
    return DRIVER_OK;
}

int max30105_set_fifo(max30105_handle_t *handle, max30105_sample_avg_t average,
                      bool rollover, uint8_t almost_full)
{
    uint8_t value;
    int ret;

    ret = max30105_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if ((uint8_t)average > (uint8_t)MAX30105_AVG_32 || almost_full > 0x0FU) {
        return (int)IOT_INVALID_PARAM;
    }

    value = (uint8_t)(((uint8_t)average << 5) & 0xE0U);
    if (rollover) {
        value |= MAX30105_FIFO_ROLLOVER_EN;
    }
    value |= (uint8_t)(almost_full & MAX30105_FIFO_A_FULL_MASK);

    MAX30105_TRY(max30105_write_u8(handle, MAX30105_REG_FIFO_CONFIG, value));
    return max30105_clear_fifo(handle);
}

int max30105_set_led_current(max30105_handle_t *handle, uint8_t red, uint8_t ir, uint8_t green)
{
    int ret = max30105_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    MAX30105_TRY(max30105_write_u8(handle, MAX30105_REG_LED1_PA, red));
    MAX30105_TRY(max30105_write_u8(handle, MAX30105_REG_LED2_PA, ir));
    return max30105_write_u8(handle, MAX30105_REG_LED3_PA, green);
}

int max30105_set_proximity_threshold(max30105_handle_t *handle, uint8_t threshold)
{
    int ret = max30105_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    return max30105_write_u8(handle, MAX30105_REG_PROX_INT_THRESH, threshold);
}

int max30105_init(max30105_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    uint8_t part_id = 0U;
    uint8_t spo2;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : MAX30105_ADDRESS_DEFAULT;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->mode = MAX30105_MODE_SPO2;
    handle->channels = 2U;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E(MAX30105_TAG, "probe failed at 0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }
    if (driver_i2c_read_reg(&handle->i2c, MAX30105_REG_PART_ID, &part_id, 1U) == DRIVER_OK &&
        part_id != MAX30105_PART_ID_VALUE) {
        LOG_E(MAX30105_TAG, "unexpected part id 0x%02X", part_id);
    }

    handle->initialized = true;
    ret = max30105_reset(handle);
    if (ret != DRIVER_OK) {
        handle->initialized = false;
        return ret;
    }

    if (max30105_set_fifo(handle, MAX30105_AVG_4, true, MAX30105_FIFO_A_FULL_15) != DRIVER_OK) {
        handle->initialized = false;
        return (int)IOT_IO_ERROR;
    }

    spo2 = (uint8_t)(MAX30105_SPO2_ADC_RANGE_4096 | MAX30105_SPO2_SR_400HZ |
                     MAX30105_SPO2_PW_411US_18BIT);
    ret = max30105_write_u8(handle, MAX30105_REG_SPO2_CONFIG, spo2);
    if (ret == DRIVER_OK) {
        ret = max30105_set_led_current(handle, 0x24U, 0x24U, 0x24U);
    }
    if (ret == DRIVER_OK) {
        ret = max30105_set_mode(handle, MAX30105_MODE_SPO2);
    }
    if (ret != DRIVER_OK) {
        handle->initialized = false;
        return ret;
    }

    LOG_I(MAX30105_TAG, "init ok at 0x%02X, part 0x%02X", handle->i2c.address, part_id);
    return DRIVER_OK;
}

int max30105_deinit(max30105_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)max30105_write_u8(handle, MAX30105_REG_MODE_CONFIG, MAX30105_MODE_SHUTDOWN);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int max30105_read(max30105_handle_t *handle, max30105_sample_t *samples,
                  size_t capacity, size_t *count)
{
    uint8_t ptr[3];
    uint8_t buf[MAX30105_FIFO_MAX_BYTES];
    size_t available;
    size_t num;
    size_t i;
    size_t stride;
    uint8_t ch;
    int ret;

    if (samples == NULL || count == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    *count = 0U;
    ret = max30105_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (capacity == 0U) {
        return (int)IOT_INVALID_PARAM;
    }

    MAX30105_TRY(max30105_read_reg(handle, MAX30105_REG_FIFO_WR_PTR, ptr, sizeof(ptr)));
    available = (size_t)((ptr[0] - ptr[2]) & MAX30105_FIFO_PTR_MASK);
    if (available == 0U) {
        return DRIVER_OK;
    }

    num = DRIVER_MIN(available, capacity);
    num = DRIVER_MIN(num, (size_t)MAX30105_FIFO_DEPTH);
    ch = (handle->channels == 0U) ? 1U : handle->channels;
    ch = (uint8_t)DRIVER_MIN(ch, (uint8_t)MAX30105_MAX_CHANNELS);
    stride = (size_t)ch * MAX30105_SAMPLE_BYTES_PER_CH;

    MAX30105_TRY(max30105_read_reg(handle, MAX30105_REG_FIFO_DATA, buf, num * stride));
    for (i = 0U; i < num; i++) {
        const uint8_t *p = &buf[i * stride];

        samples[i].red = (((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]) &
                         MAX30105_ADC_MASK;
        samples[i].ir = (ch > 1U) ?
            ((((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5]) & MAX30105_ADC_MASK) : 0U;
        samples[i].green = (ch > 2U) ?
            ((((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 8) | p[8]) & MAX30105_ADC_MASK) : 0U;
    }

    *count = num;
    return DRIVER_OK;
}

int max30105_read_temperature(max30105_handle_t *handle, float *celsius)
{
    uint8_t buf[2];
    int ret;

    if (celsius == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    ret = max30105_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    MAX30105_TRY(max30105_write_u8(handle, MAX30105_REG_TEMP_CONFIG, 0x01U));
    driver_delay_ms(MAX30105_TEMP_MEASURE_MS);
    MAX30105_TRY(max30105_read_reg(handle, MAX30105_REG_TEMP_INTR, buf, sizeof(buf)));

    *celsius = (float)(int8_t)buf[0] + ((float)buf[1] * MAX30105_TEMP_FRAC_LSB_C);
    return DRIVER_OK;
}
