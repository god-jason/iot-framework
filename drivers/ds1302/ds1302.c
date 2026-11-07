#include "ds1302.h"
#include "ds1302_register.h"
#include "iot_logger.h"

/* ---------------- BCD 辅助 ---------------- */

static uint8_t ds1302_bin_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static uint8_t ds1302_bcd_to_bin(uint8_t value)
{
    return (uint8_t)(((value >> 4) & 0x0FU) * 10U + (value & 0x0FU));
}

/* ---------------- 三线时序 ---------------- */

static void ds1302_ce(ds1302_handle_t *handle, port_gpio_level_t level)
{
    (void)driver_gpio_write(handle->ce, level);
}

static void ds1302_write_byte(ds1302_handle_t *handle, uint8_t value)
{
    uint8_t i;

    (void)driver_gpio_output(handle->io, PORT_GPIO_HIGH);
    for (i = 0U; i < 8U; i++) {
        (void)driver_gpio_write(handle->sclk, PORT_GPIO_LOW);
        (void)driver_gpio_write(handle->io,
                                ((value & (uint8_t)DRIVER_BIT(i)) != 0U) ? PORT_GPIO_HIGH
                                                                       : PORT_GPIO_LOW);
        driver_delay_us(DS1302_TICK_US);
        /* DS1302 在 SCLK 上升沿采样 */
        (void)driver_gpio_write(handle->sclk, PORT_GPIO_HIGH);
        driver_delay_us(DS1302_TICK_US);
    }
    (void)driver_gpio_write(handle->sclk, PORT_GPIO_LOW);
}

static uint8_t ds1302_read_byte(ds1302_handle_t *handle)
{
    uint8_t i;
    uint8_t value = 0U;

    (void)driver_gpio_input(handle->io, PORT_GPIO_PULLUP);
    for (i = 0U; i < 8U; i++) {
        (void)driver_gpio_write(handle->sclk, PORT_GPIO_LOW);
        driver_delay_us(DS1302_TICK_US);
        /* DS1302 在 SCLK 下降沿输出，此处采样 */
        (void)driver_gpio_write(handle->sclk, PORT_GPIO_HIGH);
        if (driver_gpio_read(handle->io) > 0) {
            value = (uint8_t)(value | (uint8_t)DRIVER_BIT(i));
        }
        driver_delay_us(DS1302_TICK_US);
    }
    (void)driver_gpio_write(handle->sclk, PORT_GPIO_LOW);
    (void)driver_gpio_output(handle->io, PORT_GPIO_HIGH);
    return value;
}

static int ds1302_write_regs(ds1302_handle_t *handle, uint8_t cmd,
                             const uint8_t *data, size_t len)
{
    size_t i;

    ds1302_ce(handle, PORT_GPIO_HIGH);
    driver_delay_us(DS1302_CE_SETTLE_US);
    ds1302_write_byte(handle, cmd);
    for (i = 0U; i < len; i++) {
        ds1302_write_byte(handle, data[i]);
    }
    ds1302_ce(handle, PORT_GPIO_LOW);
    return DRIVER_OK;
}

static int ds1302_read_regs(ds1302_handle_t *handle, uint8_t cmd, uint8_t *data, size_t len)
{
    size_t i;

    ds1302_ce(handle, PORT_GPIO_HIGH);
    driver_delay_us(DS1302_CE_SETTLE_US);
    /* 读命令 = 写命令 | 0x01 */
    ds1302_write_byte(handle, (uint8_t)(cmd | 0x01U));
    for (i = 0U; i < len; i++) {
        data[i] = ds1302_read_byte(handle);
    }
    ds1302_ce(handle, PORT_GPIO_LOW);
    return DRIVER_OK;
}

/* ---------------- 公开 API ---------------- */

int ds1302_init(ds1302_handle_t *handle, port_pin_t ce, port_pin_t sclk, port_pin_t io)
{
    if (handle == NULL || ce == 0U || sclk == 0U || io == 0U) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->ce = ce;
    handle->sclk = sclk;
    handle->io = io;
    handle->initialized = false;

    if (driver_gpio_output(ce, PORT_GPIO_LOW) != DRIVER_OK ||
        driver_gpio_output(sclk, PORT_GPIO_LOW) != DRIVER_OK ||
        driver_gpio_output(io, PORT_GPIO_HIGH) != DRIVER_OK) {
        LOG_E("ds1302", "gpio init failed");
        return (int)PORT_IO_ERROR;
    }

    handle->initialized = true;
    /* 解除写保护，便于后续设置时间 */
    (void)ds1302_set_write_protect(handle, false);
    LOG_I("ds1302", "init ok");
    return DRIVER_OK;
}

int ds1302_deinit(ds1302_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)PORT_NOT_READY;
    }

    (void)ds1302_set_write_protect(handle, true);
    ds1302_ce(handle, PORT_GPIO_LOW);
    (void)driver_gpio_input(handle->ce, PORT_GPIO_NOPULL);
    (void)driver_gpio_input(handle->sclk, PORT_GPIO_NOPULL);
    (void)driver_gpio_input(handle->io, PORT_GPIO_NOPULL);
    handle->initialized = false;
    return DRIVER_OK;
}

int ds1302_set_write_protect(ds1302_handle_t *handle, bool enable)
{
    uint8_t value = enable ? DS1302_WP_ENABLE : DS1302_WP_DISABLE;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return ds1302_write_regs(handle, DS1302_REG_WRITE_PROTECT, &value, 1U);
}

int ds1302_set_trickle_charger(ds1302_handle_t *handle, uint8_t config)
{
    uint8_t value = config;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    /* TCS 位为 0b10xx 才允许写入充电参数，否则保持关闭 */
    if (value != DS1302_TRICKLE_DISABLE && (value & 0xA0U) != 0xA0U) {
        return (int)PORT_INVALID_PARAM;
    }
    /* 涓流充电寄存器受写保护控制 */
    if (ds1302_set_write_protect(handle, false) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    return ds1302_write_regs(handle, DS1302_REG_TRICKLE_CHARGE, &value, 1U);
}

int ds1302_set_time(ds1302_handle_t *handle, const ds1302_time_t *time)
{
    uint8_t buf[7];

    if (handle == NULL || time == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (time->year < 2000U || time->year > 2099U ||
        time->month < 1U || time->month > 12U ||
        time->day < 1U || time->day > 31U ||
        time->hour > 23U || time->minute > 59U || time->second > 59U ||
        time->weekday < 1U || time->weekday > 7U) {
        return (int)PORT_INVALID_PARAM;
    }

    /* 秒寄存器 bit7 (CH) 写 0 表示启动振荡器 */
    buf[0] = ds1302_bin_to_bcd(time->second);
    buf[1] = ds1302_bin_to_bcd(time->minute);
    buf[2] = ds1302_bin_to_bcd(time->hour);       /* 24 小时制，bit7=0 */
    buf[3] = ds1302_bin_to_bcd(time->day);
    buf[4] = ds1302_bin_to_bcd(time->month);
    buf[5] = time->weekday;
    buf[6] = ds1302_bin_to_bcd((uint8_t)(time->year - 2000U));

    if (ds1302_set_write_protect(handle, false) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    return ds1302_write_regs(handle, DS1302_REG_SECONDS, buf, sizeof(buf));
}

int ds1302_get_time(ds1302_handle_t *handle, ds1302_time_t *time)
{
    uint8_t buf[7];
    uint8_t hour;
    uint8_t second;
    int ret;

    if (handle == NULL || time == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = ds1302_read_regs(handle, DS1302_REG_SECONDS, buf, sizeof(buf));
    if (ret != DRIVER_OK) {
        return ret;
    }

    second = ds1302_bcd_to_bin((uint8_t)(buf[0] & 0x7FU));
    hour = buf[2];
    if ((hour & DS1302_HOUR_12H_MODE) != 0U) {
        /* 12 小时制：bit5 = PM */
        uint8_t h12 = ds1302_bcd_to_bin((uint8_t)(hour & DS1302_HOUR_12H_MASK));
        uint8_t pm = ((hour & DS1302_HOUR_PM) != 0U) ? 1U : 0U;

        if (h12 == 12U) {
            h12 = 0U;
        }
        hour = (uint8_t)(h12 + pm * 12U);
    } else {
        hour = ds1302_bcd_to_bin((uint8_t)(hour & DS1302_HOUR_24H_MASK));
    }

    time->second = second;
    time->minute = ds1302_bcd_to_bin(buf[1]);
    time->hour = hour;
    time->day = ds1302_bcd_to_bin((uint8_t)(buf[3] & DS1302_DATE_MASK));
    time->month = ds1302_bcd_to_bin((uint8_t)(buf[4] & DS1302_MONTH_MASK));
    time->weekday = (uint8_t)(buf[5] & DS1302_WEEKDAY_MASK);
    time->year = (uint16_t)(2000U + ds1302_bcd_to_bin(buf[6]));
    return DRIVER_OK;
}

int ds1302_read_ram(ds1302_handle_t *handle, uint8_t offset, uint8_t *data, size_t len)
{
    uint8_t cmd;

    if (handle == NULL || data == NULL || !handle->initialized || len == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    if (offset >= DS1302_RAM_SIZE || (size_t)offset + len > DS1302_RAM_SIZE) {
        return (int)PORT_INVALID_PARAM;
    }

    cmd = (uint8_t)(DS1302_RAM_BASE + (uint8_t)(offset * 2U));
    return ds1302_read_regs(handle, cmd, data, len);
}

int ds1302_write_ram(ds1302_handle_t *handle, uint8_t offset, const uint8_t *data, size_t len)
{
    uint8_t cmd;

    if (handle == NULL || data == NULL || !handle->initialized || len == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    if (offset >= DS1302_RAM_SIZE || (size_t)offset + len > DS1302_RAM_SIZE) {
        return (int)PORT_INVALID_PARAM;
    }

    cmd = (uint8_t)(DS1302_RAM_BASE + (uint8_t)(offset * 2U));
    if (ds1302_set_write_protect(handle, false) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    return ds1302_write_regs(handle, cmd, data, len);
}
