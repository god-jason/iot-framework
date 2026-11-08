#include "pcf8563.h"
#include "pcf8563_register.h"
#include "log.h"

static uint8_t pcf8563_bin_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static uint8_t pcf8563_bcd_to_bin(uint8_t value)
{
    return (uint8_t)(((value >> 4) & 0x0FU) * 10U + (value & 0x0FU));
}

int pcf8563_init(pcf8563_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    uint8_t control = 0U;

    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? (uint16_t)(address & PCF8563_ADDRESS_MASK)
                                          : PCF8563_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    driver_delay_ms(PCF8563_POWER_ON_DELAY_MS);

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("pcf8563", "probe failed, addr=0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    /* 清除 TEST 与 STOP：使能正常计数 */
    if (driver_i2c_read_reg(&handle->i2c, PCF8563_REG_CONTROL1, &control, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    control = (uint8_t)(control & (uint8_t)~(PCF8563_CTRL1_TEST | PCF8563_CTRL1_STOP));
    if (driver_i2c_write_reg(&handle->i2c, PCF8563_REG_CONTROL1, &control, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    handle->initialized = true;
    LOG_I("pcf8563", "init ok");
    return DRIVER_OK;
}

int pcf8563_deinit(pcf8563_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)HAL_NOT_READY;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int pcf8563_read(pcf8563_handle_t *handle, uint8_t reg, uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || !handle->initialized || len == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, reg, data, len);
}

int pcf8563_write(pcf8563_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || !handle->initialized || len == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    return driver_i2c_write_reg(&handle->i2c, reg, data, len);
}

int pcf8563_set_time(pcf8563_handle_t *handle, const pcf8563_time_t *time)
{
    uint8_t buf[7];
    uint8_t control = 0U;

    if (handle == NULL || time == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (time->year < 2000U || time->year > 2199U ||
        time->month < 1U || time->month > 12U ||
        time->day < 1U || time->day > 31U ||
        time->hour > 23U || time->minute > 59U || time->second > 59U ||
        time->weekday > 6U) {
        return (int)HAL_INVALID_PARAM;
    }

    /* 清除 STOP 位，保证写入后立即开始计数 */
    if (driver_i2c_read_reg(&handle->i2c, PCF8563_REG_CONTROL1, &control, 1U) == DRIVER_OK) {
        control = (uint8_t)(control & (uint8_t)~PCF8563_CTRL1_STOP);
        (void)driver_i2c_write_reg(&handle->i2c, PCF8563_REG_CONTROL1, &control, 1U);
    }

    buf[0] = pcf8563_bin_to_bcd(time->second);   /* 写时 VL 位置 0 */
    buf[1] = pcf8563_bin_to_bcd(time->minute);
    buf[2] = pcf8563_bin_to_bcd(time->hour);
    buf[3] = pcf8563_bin_to_bcd(time->day);
    buf[4] = time->weekday;
    buf[5] = pcf8563_bin_to_bcd(time->month);
    if (time->year >= 2100U) {
        buf[5] = (uint8_t)(buf[5] | PCF8563_CENTURY_BIT);
    }
    buf[6] = pcf8563_bin_to_bcd((uint8_t)(time->year % 100U));

    if (driver_i2c_write_reg(&handle->i2c, PCF8563_REG_SECONDS, buf, sizeof(buf)) != DRIVER_OK) {
        LOG_E("pcf8563", "write time failed");
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}

int pcf8563_get_time(pcf8563_handle_t *handle, pcf8563_time_t *time)
{
    uint8_t buf[7];
    uint16_t year;
    bool voltage_low;

    if (handle == NULL || time == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    if (driver_i2c_read_reg(&handle->i2c, PCF8563_REG_SECONDS, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    voltage_low = ((buf[0] & PCF8563_VL_FLAG) != 0U);
    year = (uint16_t)(2000U + pcf8563_bcd_to_bin(buf[6]));
    if ((buf[5] & PCF8563_CENTURY_BIT) != 0U) {
        year = (uint16_t)(year + 100U);
    }

    time->second = pcf8563_bcd_to_bin((uint8_t)(buf[0] & 0x7FU));
    time->minute = pcf8563_bcd_to_bin((uint8_t)(buf[1] & PCF8563_MINUTE_MASK));
    time->hour = pcf8563_bcd_to_bin((uint8_t)(buf[2] & PCF8563_HOUR_MASK));
    time->day = pcf8563_bcd_to_bin((uint8_t)(buf[3] & PCF8563_DAY_MASK));
    time->weekday = (uint8_t)(buf[4] & PCF8563_WEEKDAY_MASK);
    time->month = pcf8563_bcd_to_bin((uint8_t)(buf[5] & PCF8563_MONTH_MASK));
    time->year = year;

    /* VL=1 表示供电电压曾过低，时间可能已丢失 */
    if (voltage_low) {
        LOG_W("pcf8563", "voltage low flag set, time may be invalid");
        return (int)HAL_NOT_READY;
    }
    return DRIVER_OK;
}

int pcf8563_set_clkout(pcf8563_handle_t *handle, uint8_t freq)
{
    uint8_t value;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    if (freq == 0xFFU) {
        value = 0x00U;                          /* FE=0 关闭输出 */
    } else if (freq <= PCF8563_CLKOUT_1HZ) {
        value = (uint8_t)(PCF8563_CLKOUT_ENABLE | freq);
    } else {
        return (int)HAL_INVALID_PARAM;
    }

    if (driver_i2c_write_reg(&handle->i2c, PCF8563_REG_CLKOUT, &value, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return DRIVER_OK;
}
