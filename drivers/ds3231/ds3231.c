#include "ds3231.h"
#include "ds3231_register.h"
#include "log.h"

static uint8_t ds3231_bin_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static uint8_t ds3231_bcd_to_bin(uint8_t value)
{
    return (uint8_t)(((value >> 4) & 0x0FU) * 10U + (value & 0x0FU));
}

int ds3231_init(ds3231_handle_t *handle, hal_i2c_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? (uint16_t)(address & DS3231_ADDRESS_MASK)
                                          : DS3231_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = HAL_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    hal_delay_ms(DS3231_POWER_ON_DELAY_MS);

    if (hal_i2c_dev_probe(&handle->i2c) != HAL_OK) {
        LOG_E("ds3231", "probe failed, addr=0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    handle->initialized = true;
    LOG_I("ds3231", "init ok");
    return HAL_OK;
}

int ds3231_deinit(ds3231_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)HAL_NOT_READY;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return HAL_OK;
}

int ds3231_read(ds3231_handle_t *handle, uint8_t reg, uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || !handle->initialized || len == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    return hal_i2c_dev_read_reg(&handle->i2c, reg, data, len);
}

int ds3231_write(ds3231_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || !handle->initialized || len == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    return hal_i2c_dev_write_reg(&handle->i2c, reg, data, len);
}

int ds3231_set_time(ds3231_handle_t *handle, const ds3231_time_t *time)
{
    uint8_t buf[7];
    uint8_t control = 0U;
    uint8_t status = 0U;

    if (handle == NULL || time == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (time->year < 2000U || time->year > 2199U ||
        time->month < 1U || time->month > 12U ||
        time->day < 1U || time->day > 31U ||
        time->hour > 23U || time->minute > 59U || time->second > 59U ||
        time->weekday < 1U || time->weekday > 7U) {
        return (int)HAL_INVALID_PARAM;
    }

    /* 清除 EOSC，保证电池供电时振荡器继续运行 */
    if (hal_i2c_dev_read_reg(&handle->i2c, DS3231_REG_CONTROL, &control, 1U) == HAL_OK) {
        control = (uint8_t)(control & (uint8_t)~DS3231_CTRL_EOSC);
        (void)hal_i2c_dev_write_reg(&handle->i2c, DS3231_REG_CONTROL, &control, 1U);
    }

    buf[0] = ds3231_bin_to_bcd(time->second);
    buf[1] = ds3231_bin_to_bcd(time->minute);
    buf[2] = ds3231_bin_to_bcd(time->hour);
    buf[3] = time->weekday;
    buf[4] = ds3231_bin_to_bcd(time->day);
    buf[5] = ds3231_bin_to_bcd(time->month);
    if (time->year >= 2100U) {
        buf[5] = (uint8_t)(buf[5] | DS3231_MONTH_CENTURY);
    }
    buf[6] = ds3231_bin_to_bcd((uint8_t)(time->year % 100U));

    if (hal_i2c_dev_write_reg(&handle->i2c, DS3231_REG_SECONDS, buf, sizeof(buf)) != HAL_OK) {
        LOG_E("ds3231", "write time failed");
        return (int)HAL_IO_ERROR;
    }

    /* 清除 OSF：时间已被重新设置，停振标志不再有效 */
    if (hal_i2c_dev_read_reg(&handle->i2c, DS3231_REG_STATUS, &status, 1U) == HAL_OK &&
        (status & DS3231_STATUS_OSF) != 0U) {
        status = (uint8_t)(status & (uint8_t)~DS3231_STATUS_OSF);
        (void)hal_i2c_dev_write_reg(&handle->i2c, DS3231_REG_STATUS, &status, 1U);
    }
    return HAL_OK;
}

int ds3231_get_time(ds3231_handle_t *handle, ds3231_time_t *time)
{
    uint8_t buf[7];
    uint8_t hour;
    uint16_t year;

    if (handle == NULL || time == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    if (hal_i2c_dev_read_reg(&handle->i2c, DS3231_REG_SECONDS, buf, sizeof(buf)) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }

    hour = buf[2];
    if ((hour & DS3231_HOUR_12H_MODE) != 0U) {
        uint8_t h12 = ds3231_bcd_to_bin((uint8_t)(hour & DS3231_HOUR_12H_MASK));
        uint8_t pm = ((hour & DS3231_HOUR_PM) != 0U) ? 1U : 0U;

        if (h12 == 12U) {
            h12 = 0U;
        }
        hour = (uint8_t)(h12 + pm * 12U);
    } else {
        hour = ds3231_bcd_to_bin((uint8_t)(hour & DS3231_HOUR_24H_MASK));
    }

    year = (uint16_t)(2000U + ds3231_bcd_to_bin(buf[6]));
    if ((buf[5] & DS3231_MONTH_CENTURY) != 0U) {
        year = (uint16_t)(year + 100U);
    }

    time->second = ds3231_bcd_to_bin((uint8_t)(buf[0] & 0x7FU));
    time->minute = ds3231_bcd_to_bin(buf[1]);
    time->hour = hour;
    time->weekday = (uint8_t)(buf[3] & DS3231_WEEKDAY_MASK);
    time->day = ds3231_bcd_to_bin((uint8_t)(buf[4] & DS3231_DATE_MASK));
    time->month = ds3231_bcd_to_bin((uint8_t)(buf[5] & DS3231_MONTH_MASK));
    time->year = year;
    return HAL_OK;
}

int ds3231_get_temperature(ds3231_handle_t *handle, float *celsius)
{
    uint8_t reg[2];
    uint8_t status = 0U;
    int8_t integer;

    if (handle == NULL || celsius == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    /* BSY=1 表示正在进行温度转换，等待其结束 */
    if (hal_i2c_dev_read_reg(&handle->i2c, DS3231_REG_STATUS, &status, 1U) == HAL_OK &&
        (status & DS3231_STATUS_BSY) != 0U) {
        hal_delay_ms(DS3231_CONV_DELAY_MS);
    }

    if (hal_i2c_dev_read_reg(&handle->i2c, DS3231_REG_TEMP_MSB, reg, sizeof(reg)) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }

    integer = (int8_t)reg[0];
    *celsius = (float)integer + (float)(reg[1] >> 6) * DS3231_LSB_CELSIUS;
    return HAL_OK;
}

int ds3231_set_square_wave(ds3231_handle_t *handle, uint8_t freq)
{
    uint8_t control = 0U;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (freq > DS3231_SQW_8192HZ && freq != DS3231_SQW_DISABLE) {
        return (int)HAL_INVALID_PARAM;
    }

    if (hal_i2c_dev_read_reg(&handle->i2c, DS3231_REG_CONTROL, &control, 1U) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }

    /* 清 RS2/RS1/BBSQW 位后重新配置 */
    control = (uint8_t)(control & (uint8_t)~(DS3231_CTRL_RS2 | DS3231_CTRL_RS1 |
                                             DS3231_CTRL_BBSQW));
    if (freq != DS3231_SQW_DISABLE) {
        control = (uint8_t)(control | freq);
        control = (uint8_t)(control | DS3231_CTRL_BBSQW);   /* 电池供电时也输出 */
    }

    if (hal_i2c_dev_write_reg(&handle->i2c, DS3231_REG_CONTROL, &control, 1U) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }
    return HAL_OK;
}

int ds3231_clear_oscillator_flag(ds3231_handle_t *handle, uint8_t *was_stopped)
{
    uint8_t status = 0U;

    if (handle == NULL || was_stopped == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    if (hal_i2c_dev_read_reg(&handle->i2c, DS3231_REG_STATUS, &status, 1U) != HAL_OK) {
        return (int)HAL_IO_ERROR;
    }

    *was_stopped = ((status & DS3231_STATUS_OSF) != 0U) ? 1U : 0U;
    if (*was_stopped != 0U) {
        status = (uint8_t)(status & (uint8_t)~DS3231_STATUS_OSF);
        if (hal_i2c_dev_write_reg(&handle->i2c, DS3231_REG_STATUS, &status, 1U) != HAL_OK) {
            return (int)HAL_IO_ERROR;
        }
    }
    return HAL_OK;
}
