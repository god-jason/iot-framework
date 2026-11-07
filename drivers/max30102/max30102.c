#include "max30102.h"
#include "max30102_register.h"
#include "log.h"

#define MAX30102_TAG "max30102"
#define MAX30102_TRY(expr)          \
    do {                            \
        int r_ = (expr);            \
        if (r_ != DRIVER_OK) {      \
            return r_;              \
        }                           \
    } while (0)

static int max30102_check(const max30102_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int max30102_write(max30102_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len)
{
    if (handle == NULL || handle->i2c.handle == NULL || data == NULL || len == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    return driver_i2c_write_reg(&handle->i2c, reg, data, len);
}

int max30102_read_reg(max30102_handle_t *handle, uint8_t reg, uint8_t *data, size_t len)
{
    if (handle == NULL || handle->i2c.handle == NULL || data == NULL || len == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, reg, data, len);
}

static int max30102_write_u8(max30102_handle_t *handle, uint8_t reg, uint8_t value)
{
    return max30102_write(handle, reg, &value, 1U);
}

int max30102_clear_fifo(max30102_handle_t *handle)
{
    int ret = max30102_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    MAX30102_TRY(max30102_write_u8(handle, MAX30102_REG_FIFO_WR_PTR, 0x00U));
    MAX30102_TRY(max30102_write_u8(handle, MAX30102_REG_OVF_COUNTER, 0x00U));
    return max30102_write_u8(handle, MAX30102_REG_FIFO_RD_PTR, 0x00U);
}

int max30102_reset(max30102_handle_t *handle)
{
    uint8_t mode = 0U;
    uint32_t start;
    int ret;

    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = max30102_write_u8(handle, MAX30102_REG_MODE_CONFIG, MAX30102_MODE_RESET);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 复位位自清零即完成 */
    start = driver_tick_ms();
    for (;;) {
        ret = max30102_read_reg(handle, MAX30102_REG_MODE_CONFIG, &mode, 1U);
        if (ret != DRIVER_OK) {
            return ret;
        }
        if ((mode & MAX30102_MODE_RESET) == 0U) {
            return DRIVER_OK;
        }
        if ((uint32_t)(driver_tick_ms() - start) > 50U) {
            return (int)PORT_TIMEOUT;
        }
        driver_delay_ms(1U);
    }
}

int max30102_set_mode(max30102_handle_t *handle, max30102_mode_t mode)
{
    int ret = max30102_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (mode != MAX30102_MODE_HR_ONLY && mode != MAX30102_MODE_SPO2 &&
        mode != MAX30102_MODE_MULTI_LED) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = max30102_write_u8(handle, MAX30102_REG_MODE_CONFIG, (uint8_t)mode);
    if (ret != DRIVER_OK) {
        return ret;
    }
    handle->mode = mode;
    /* HR 模式仅 RED 通道；SpO2 / Multi-LED 为 2 个通道 */
    handle->channels = (mode == MAX30102_MODE_HR_ONLY) ? 1U : 2U;
    return DRIVER_OK;
}

int max30102_set_fifo(max30102_handle_t *handle, max30102_sample_avg_t average,
                      bool rollover, uint8_t almost_full)
{
    uint8_t value;
    int ret;

    ret = max30102_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if ((uint8_t)average > (uint8_t)MAX30102_AVG_32 || almost_full > 0x0FU) {
        return (int)PORT_INVALID_PARAM;
    }

    value = (uint8_t)(((uint8_t)average << 5) & 0xE0U);
    if (rollover) {
        value |= MAX30102_FIFO_ROLLOVER_EN;
    }
    value |= (uint8_t)(almost_full & MAX30102_FIFO_A_FULL_MASK);

    MAX30102_TRY(max30102_write_u8(handle, MAX30102_REG_FIFO_CONFIG, value));
    return max30102_clear_fifo(handle);
}

int max30102_set_led_current(max30102_handle_t *handle, uint8_t red, uint8_t ir)
{
    int ret = max30102_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    MAX30102_TRY(max30102_write_u8(handle, MAX30102_REG_LED1_PA, red));
    return max30102_write_u8(handle, MAX30102_REG_LED2_PA, ir);
}

int max30102_init(max30102_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint8_t part_id = 0U;
    uint8_t spo2;
    int ret;

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : MAX30102_ADDRESS_DEFAULT;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->mode = MAX30102_MODE_SPO2;
    handle->channels = 2U;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E(MAX30102_TAG, "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }
    if (driver_i2c_read_reg(&handle->i2c, MAX30102_REG_PART_ID, &part_id, 1U) == DRIVER_OK &&
        part_id != MAX30102_PART_ID_VALUE) {
        LOG_E(MAX30102_TAG, "unexpected part id 0x%02X", part_id);
    }

    handle->initialized = true; /* reset / clear 需要该标志 */
    ret = max30102_reset(handle);
    if (ret != DRIVER_OK) {
        handle->initialized = false;
        return ret;
    }

    /* FIFO：4 次平均、回绕使能、almost full = 15 */
    if (max30102_set_fifo(handle, MAX30102_AVG_4, true, MAX30102_FIFO_A_FULL_15) != DRIVER_OK) {
        handle->initialized = false;
        return (int)PORT_IO_ERROR;
    }

    /* SpO2 配置：4096nA、100Hz、411us/18bit */
    spo2 = (uint8_t)(MAX30102_SPO2_ADC_RANGE_4096 | MAX30102_SPO2_SR_100HZ |
                     MAX30102_SPO2_PW_411US_18BIT);
    ret = max30102_write_u8(handle, MAX30102_REG_SPO2_CONFIG, spo2);
    if (ret == DRIVER_OK) {
        ret = max30102_set_led_current(handle, 0x24U, 0x24U);
    }
    if (ret == DRIVER_OK) {
        ret = max30102_set_mode(handle, MAX30102_MODE_SPO2);
    }
    if (ret != DRIVER_OK) {
        handle->initialized = false;
        return ret;
    }

    LOG_I(MAX30102_TAG, "init ok at 0x%02X, part 0x%02X", handle->i2c.address, part_id);
    return DRIVER_OK;
}

int max30102_deinit(max30102_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)max30102_write_u8(handle, MAX30102_REG_MODE_CONFIG, MAX30102_MODE_SHUTDOWN);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int max30102_read(max30102_handle_t *handle, max30102_sample_t *samples,
                  size_t capacity, size_t *count)
{
    uint8_t ptr[3];
    uint8_t buf[MAX30102_FIFO_MAX_BYTES];
    size_t available;
    size_t num;
    size_t i;
    size_t stride;
    uint8_t ch;
    int ret;

    if (samples == NULL || count == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    *count = 0U;
    ret = max30102_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (capacity == 0U) {
        return (int)PORT_INVALID_PARAM;
    }

    MAX30102_TRY(max30102_read_reg(handle, MAX30102_REG_FIFO_WR_PTR, ptr, sizeof(ptr)));
    available = (size_t)((ptr[0] - ptr[2]) & MAX30102_FIFO_PTR_MASK);
    if (available == 0U) {
        return DRIVER_OK;
    }

    num = DRIVER_MIN(available, capacity);
    num = DRIVER_MIN(num, (size_t)MAX30102_FIFO_DEPTH);
    ch = (handle->channels == 0U) ? 1U : handle->channels;
    stride = (size_t)ch * MAX30102_SAMPLE_BYTES_PER_CH;

    MAX30102_TRY(max30102_read_reg(handle, MAX30102_REG_FIFO_DATA, buf, num * stride));
    for (i = 0U; i < num; i++) {
        const uint8_t *p = &buf[i * stride];

        samples[i].red = (((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]) &
                         MAX30102_ADC_MASK;
        samples[i].ir = (ch > 1U) ?
            ((((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5]) & MAX30102_ADC_MASK) : 0U;
    }

    *count = num;
    return DRIVER_OK;
}

int max30102_read_temperature(max30102_handle_t *handle, float *celsius)
{
    uint8_t buf[2];
    int ret;

    if (celsius == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = max30102_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    MAX30102_TRY(max30102_write_u8(handle, MAX30102_REG_TEMP_CONFIG, 0x01U));
    driver_delay_ms(MAX30102_TEMP_MEASURE_MS);
    MAX30102_TRY(max30102_read_reg(handle, MAX30102_REG_TEMP_INTR, buf, sizeof(buf)));

    *celsius = (float)(int8_t)buf[0] + ((float)buf[1] * MAX30102_TEMP_FRAC_LSB_C);
    return DRIVER_OK;
}
