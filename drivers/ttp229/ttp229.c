#include "ttp229.h"
#include "ttp229_register.h"
#include "log.h"

static int ttp229_read_i2c(ttp229_handle_t *handle, uint16_t *keys)
{
    uint8_t buf[TTP229_I2C_FRAME_LEN] = { 0U, 0U };

    if (driver_i2c_read(&handle->i2c, buf, TTP229_I2C_FRAME_LEN) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    /* 第 1 字节为 K1~K8，第 2 字节为 K9~K16，bit0 对应低位键号 */
    *keys = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
    return DRIVER_OK;
}

static int ttp229_read_gpio(ttp229_handle_t *handle, uint16_t *keys)
{
    uint16_t bits = 0U;
    uint8_t i;
    uint32_t start = driver_tick_ms();

    /* SDO 拉低表示数据有效 */
    for (;;) {
        int level = driver_gpio_read(handle->sdo);

        if (level < 0) {
            return (int)PORT_IO_ERROR;
        }
        if (level == 0) {
            break;
        }
        if ((uint32_t)(driver_tick_ms() - start) > TTP229_READY_TIMEOUT_MS) {
            return (int)PORT_TIMEOUT;
        }
        driver_delay_us(100U);
    }

    /*
     * 时钟输出 16 位键值，SDO 在高电平时采样。
     * 需按 datasheet 补充：不同封装的位数（8/16）、采样边沿与
     * 帧结束信号（DV）时序，使用前请对照模块手册确认。
     */
    for (i = 0U; i < TTP229_KEY_COUNT; i++) {
        int level;

        (void)driver_gpio_write(handle->scl, PORT_GPIO_HIGH);
        driver_delay_us(TTP229_SCL_HIGH_US);
        level = driver_gpio_read(handle->sdo);
        if (level < 0) {
            (void)driver_gpio_write(handle->scl, PORT_GPIO_LOW);
            return (int)PORT_IO_ERROR;
        }
        if (level != 0) {
            bits = (uint16_t)(bits | (uint16_t)(1U << i));
        }
        (void)driver_gpio_write(handle->scl, PORT_GPIO_LOW);
        driver_delay_us(TTP229_SCL_LOW_US);
    }

    *keys = bits;
    return DRIVER_OK;
}

int ttp229_init_i2c(ttp229_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    uint8_t buf[TTP229_I2C_FRAME_LEN] = { 0U, 0U };

    if (handle == NULL || i2c == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->bus = TTP229_BUS_I2C;
    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : TTP229_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->scl = 0U;
    handle->sdo = 0U;
    handle->initialized = false;

    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("ttp229", "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }

    /* 读一次键值，确认器件可读 */
    if (driver_i2c_read(&handle->i2c, buf, TTP229_I2C_FRAME_LEN) != DRIVER_OK) {
        return (int)PORT_NOT_FOUND;
    }

    driver_delay_ms(TTP229_POWER_ON_MS);
    handle->initialized = true;
    LOG_I("ttp229", "init ok (i2c) at 0x%02X", handle->i2c.address);
    return DRIVER_OK;
}

int ttp229_init_gpio(ttp229_handle_t *handle, port_pin_t scl, port_pin_t sdo)
{
    if (handle == NULL || scl == 0U || sdo == 0U || scl == sdo) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->bus = TTP229_BUS_GPIO_2WIRE;
    handle->i2c.handle = NULL;
    handle->i2c.address = 0U;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->scl = scl;
    handle->sdo = sdo;
    handle->initialized = false;

    if (driver_gpio_output(scl, PORT_GPIO_LOW) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (driver_gpio_input(sdo, PORT_GPIO_PULLUP) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }

    driver_delay_ms(TTP229_POWER_ON_MS);
    handle->initialized = true;
    LOG_I("ttp229", "init ok (2-wire)");
    return DRIVER_OK;
}

int ttp229_deinit(ttp229_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized && handle->bus == TTP229_BUS_GPIO_2WIRE) {
        (void)driver_gpio_write(handle->scl, PORT_GPIO_LOW);
        (void)port_pin_deinit(handle->scl);
        (void)port_pin_deinit(handle->sdo);
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}

int ttp229_read_keys(ttp229_handle_t *handle, uint16_t *keys)
{
    if (handle == NULL || keys == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    if (handle->bus == TTP229_BUS_I2C) {
        return ttp229_read_i2c(handle, keys);
    }
    return ttp229_read_gpio(handle, keys);
}

int ttp229_read_key(ttp229_handle_t *handle, uint8_t *key)
{
    uint16_t keys = 0U;
    uint8_t i;
    int ret;

    if (handle == NULL || key == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = ttp229_read_keys(handle, &keys);
    if (ret != DRIVER_OK) {
        return ret;
    }

    *key = 0U;
    for (i = 0U; i < TTP229_KEY_COUNT; i++) {
        if ((keys & (uint16_t)(1U << i)) != 0U) {
            *key = (uint8_t)(i + 1U);
            break;
        }
    }
    return DRIVER_OK;
}
