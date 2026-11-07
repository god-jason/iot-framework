#include "ds1307.h"
#include "ds1307_register.h"
#include "log.h"

static uint8_t ds1307_bin_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static uint8_t ds1307_bcd_to_bin(uint8_t value)
{
    return (uint8_t)(((value >> 4) & 0x0FU) * 10U + (value & 0x0FU));
}

int ds1307_init(ds1307_handle_t *handle, iot_i2c_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) {
        return (int)IOT_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? (uint16_t)(address & DS1307_ADDRESS_MASK)
                                          : DS1307_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->initialized = false;

    driver_delay_ms(DS1307_POWER_ON_DELAY_MS);

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("ds1307", "probe failed, addr=0x%02X", handle->i2c.address);
        return (int)IOT_NOT_FOUND;
    }

    handle->initialized = true;
    LOG_I("ds1307", "init ok");
    return DRIVER_OK;
}

int ds1307_deinit(ds1307_handle_t *handle)
{
    if (handle == NULL) {
        return (int)IOT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)IOT_NOT_READY;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int ds1307_read(ds1307_handle_t *handle, uint8_t reg, uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || !handle->initialized || len == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    return driver_i2c_read_reg(&handle->i2c, reg, data, len);
}

int ds1307_write(ds1307_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || !handle->initialized || len == 0U) {
        return (int)IOT_INVALID_PARAM;
    }
    return driver_i2c_write_reg(&handle->i2c, reg, data, len);
}

int ds1307_set_time(ds1307_handle_t *handle, const ds1307_time_t *time)
{
    uint8_t buf[7];

    if (handle == NULL || time == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }
    if (time->year < 2000U || time->year > 2099U ||
        time->month < 1U || time->month > 12U ||
        time->day < 1U || time->day > 31U ||
        time->hour > 23U || time->minute > 59U || time->second > 59U ||
        time->weekday < 1U || time->weekday > 7U) {
        return (int)IOT_INVALID_PARAM;
    }

    /* 秒寄存器 bit7(CH)=0 启动振荡器；小时寄存器 24 小时制 */
    buf[0] = ds1307_bin_to_bcd(time->second);
    buf[1] = ds1307_bin_to_bcd(time->minute);
    buf[2] = ds1307_bin_to_bcd(time->hour);
    buf[3] = time->weekday;
    buf[4] = ds1307_bin_to_bcd(time->day);
    buf[5] = ds1307_bin_to_bcd(time->month);
    buf[6] = ds1307_bin_to_bcd((uint8_t)(time->year - 2000U));

    if (driver_i2c_write_reg(&handle->i2c, DS1307_REG_SECONDS, buf, sizeof(buf)) != DRIVER_OK) {
        LOG_E("ds1307", "write time failed");
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

int ds1307_get_time(ds1307_handle_t *handle, ds1307_time_t *time)
{
    uint8_t buf[7];
    uint8_t hour;

    if (handle == NULL || time == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    if (driver_i2c_read_reg(&handle->i2c, DS1307_REG_SECONDS, buf, sizeof(buf)) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }

    hour = buf[2];
    if ((hour & DS1307_HOUR_12H_MODE) != 0U) {
        uint8_t h12 = ds1307_bcd_to_bin((uint8_t)(hour & DS1307_HOUR_12H_MASK));
        uint8_t pm = ((hour & DS1307_HOUR_PM) != 0U) ? 1U : 0U;

        if (h12 == 12U) {
            h12 = 0U;
        }
        hour = (uint8_t)(h12 + pm * 12U);
    } else {
        hour = ds1307_bcd_to_bin((uint8_t)(hour & DS1307_HOUR_24H_MASK));
    }

    time->second = ds1307_bcd_to_bin((uint8_t)(buf[0] & 0x7FU));
    time->minute = ds1307_bcd_to_bin(buf[1]);
    time->hour = hour;
    time->weekday = (uint8_t)(buf[3] & DS1307_WEEKDAY_MASK);
    time->day = ds1307_bcd_to_bin((uint8_t)(buf[4] & DS1307_DATE_MASK));
    time->month = ds1307_bcd_to_bin((uint8_t)(buf[5] & DS1307_MONTH_MASK));
    time->year = (uint16_t)(2000U + ds1307_bcd_to_bin(buf[6]));
    return DRIVER_OK;
}

int ds1307_set_square_wave(ds1307_handle_t *handle, uint8_t rate)
{
    uint8_t ctrl = 0U;

    if (handle == NULL || !handle->initialized) {
        return (int)IOT_INVALID_PARAM;
    }

    if (rate == DS1307_SQW_DISABLE) {
        ctrl = 0U;
    } else if (rate <= DS1307_SQW_32768HZ) {
        ctrl = (uint8_t)(DS1307_CTRL_SQWE | (rate & DS1307_CTRL_RS_MASK));
    } else {
        return (int)IOT_INVALID_PARAM;
    }

    if (driver_i2c_write_reg(&handle->i2c, DS1307_REG_CONTROL, &ctrl, 1U) != DRIVER_OK) {
        return (int)IOT_IO_ERROR;
    }
    return DRIVER_OK;
}

int ds1307_read_ram(ds1307_handle_t *handle, uint8_t offset, uint8_t *data, size_t len)
{
    if (offset >= DS1307_RAM_SIZE || (size_t)offset + len > DS1307_RAM_SIZE) {
        return (int)IOT_INVALID_PARAM;
    }
    return ds1307_read(handle, (uint8_t)(DS1307_REG_RAM_BASE + offset), data, len);
}

int ds1307_write_ram(ds1307_handle_t *handle, uint8_t offset, const uint8_t *data, size_t len)
{
    if (offset >= DS1307_RAM_SIZE || (size_t)offset + len > DS1307_RAM_SIZE) {
        return (int)IOT_INVALID_PARAM;
    }
    return ds1307_write(handle, (uint8_t)(DS1307_REG_RAM_BASE + offset), data, len);
}
