#include "common/driver_io.h"
#include "hal_os.h"

/* ---------------- I2C ---------------- */

int driver_i2c_write(driver_i2c_t *bus, const uint8_t *data, size_t len)
{
    if (bus == NULL || bus->handle == NULL || (data == NULL && len > 0U)) {
        return (int)HAL_INVALID_PARAM;
    }
    return (int)hal_i2c_master_transmit(bus->handle, bus->address, data, len, bus->timeout_ms);
}

int driver_i2c_read(driver_i2c_t *bus, uint8_t *data, size_t len)
{
    if (bus == NULL || bus->handle == NULL || (data == NULL && len > 0U)) {
        return (int)HAL_INVALID_PARAM;
    }
    return (int)hal_i2c_master_receive(bus->handle, bus->address, data, len, bus->timeout_ms);
}

int driver_i2c_write_reg(driver_i2c_t *bus, uint8_t reg, const uint8_t *data, size_t len)
{
    if (bus == NULL || bus->handle == NULL || (data == NULL && len > 0U)) {
        return (int)HAL_INVALID_PARAM;
    }
    return (int)hal_i2c_mem_write(bus->handle, bus->address, reg, 1U, data, len,
                                   bus->timeout_ms);
}

int driver_i2c_read_reg(driver_i2c_t *bus, uint8_t reg, uint8_t *data, size_t len)
{
    if (bus == NULL || bus->handle == NULL || (data == NULL && len > 0U)) {
        return (int)HAL_INVALID_PARAM;
    }
    return (int)hal_i2c_mem_read(bus->handle, bus->address, reg, 1U, data, len,
                                  bus->timeout_ms);
}

int driver_i2c_write_reg16(driver_i2c_t *bus, uint16_t reg, const uint8_t *data, size_t len)
{
    if (bus == NULL || bus->handle == NULL || (data == NULL && len > 0U)) {
        return (int)HAL_INVALID_PARAM;
    }
    return (int)hal_i2c_mem_write(bus->handle, bus->address, reg, 2U, data, len,
                                   bus->timeout_ms);
}

int driver_i2c_read_reg16(driver_i2c_t *bus, uint16_t reg, uint8_t *data, size_t len)
{
    if (bus == NULL || bus->handle == NULL || (data == NULL && len > 0U)) {
        return (int)HAL_INVALID_PARAM;
    }
    return (int)hal_i2c_mem_read(bus->handle, bus->address, reg, 2U, data, len,
                                  bus->timeout_ms);
}

int driver_i2c_probe(const driver_i2c_t *bus)
{
    if (bus == NULL || bus->handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return (int)hal_i2c_is_device_ready(bus->handle, bus->address, 3U, bus->timeout_ms);
}

/* ---------------- SPI ---------------- */

void driver_spi_select(driver_spi_t *bus)
{
    if (bus != NULL && bus->cs != 0U) {
        (void)hal_gpio_reset(bus->cs);
    }
}

void driver_spi_deselect(driver_spi_t *bus)
{
    if (bus != NULL && bus->cs != 0U) {
        (void)hal_gpio_set(bus->cs);
    }
}

int driver_spi_transfer(driver_spi_t *bus, const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (bus == NULL || bus->handle == NULL || len == 0U ||
        (tx == NULL && rx == NULL)) {
        return (int)HAL_INVALID_PARAM;
    }
    return (int)hal_spi_transfer(bus->handle, tx, rx, len, bus->timeout_ms);
}

int driver_spi_write(driver_spi_t *bus, const uint8_t *data, size_t len)
{
    if (bus == NULL || bus->handle == NULL || (data == NULL && len > 0U)) {
        return (int)HAL_INVALID_PARAM;
    }
    if (len == 0U) {
        return DRIVER_OK;
    }
    return (int)hal_spi_transmit(bus->handle, data, len, bus->timeout_ms);
}

int driver_spi_read(driver_spi_t *bus, uint8_t *data, size_t len)
{
    if (bus == NULL || bus->handle == NULL || (data == NULL && len > 0U)) {
        return (int)HAL_INVALID_PARAM;
    }
    if (len == 0U) {
        return DRIVER_OK;
    }
    return (int)hal_spi_receive(bus->handle, data, len, bus->timeout_ms);
}

int driver_spi_write_cs(driver_spi_t *bus, const uint8_t *data, size_t len)
{
    int ret;

    driver_spi_select(bus);
    ret = driver_spi_write(bus, data, len);
    driver_spi_deselect(bus);
    return ret;
}

int driver_spi_read_cs(driver_spi_t *bus, uint8_t *data, size_t len)
{
    int ret;

    driver_spi_select(bus);
    ret = driver_spi_read(bus, data, len);
    driver_spi_deselect(bus);
    return ret;
}

/* ---------------- GPIO ---------------- */

int driver_gpio_deinit(hal_pin_t pin)
{
    if (pin == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    return (int)hal_gpio_deinit(pin);
}

int driver_gpio_output(hal_pin_t pin, hal_gpio_level_t initial)
{
    hal_gpio_config_t cfg;

    cfg.mode = HAL_GPIO_MODE_OUTPUT;
    cfg.output = HAL_GPIO_OUTPUT_PP;
    cfg.pull = HAL_GPIO_NOPULL;
    cfg.speed = HAL_GPIO_SPEED_HIGH;
    cfg.alternate = 0U;
    cfg.initial_level = initial;

    return (int)hal_gpio_init(pin, &cfg);
}

int driver_gpio_input(hal_pin_t pin, hal_gpio_pull_t pull)
{
    hal_gpio_config_t cfg;

    cfg.mode = HAL_GPIO_MODE_INPUT;
    cfg.output = HAL_GPIO_OUTPUT_PP;
    cfg.pull = pull;
    cfg.speed = HAL_GPIO_SPEED_LOW;
    cfg.alternate = 0U;
    cfg.initial_level = HAL_GPIO_LOW;

    return (int)hal_gpio_init(pin, &cfg);
}

int driver_gpio_write(hal_pin_t pin, hal_gpio_level_t level)
{
    return (int)hal_gpio_write(pin, level);
}

int driver_gpio_toggle(hal_pin_t pin)
{
    return (int)hal_gpio_toggle(pin);
}

int driver_gpio_read(hal_pin_t pin)
{
    hal_gpio_level_t level = HAL_GPIO_LOW;
    hal_status_t status = hal_gpio_read(pin, &level);

    if (status != HAL_OK) {
        return (int)status;
    }
    return (level == HAL_GPIO_HIGH) ? 1 : 0;
}

/* ---------------- 时间 ---------------- */

void driver_delay_ms(uint32_t ms)
{
    hal_delay_ms(ms);
}

void driver_delay_us(uint32_t us)
{
    hal_delay_us(us);
}

uint32_t driver_tick_ms(void)
{
    return hal_tick_get();
}

/* ---------------- 工具 ---------------- */

uint8_t driver_crc8(const uint8_t *data, size_t len, uint8_t poly, uint8_t init)
{
    uint8_t crc = init;
    size_t i;
    uint8_t b;

    if (data == NULL) {
        return crc;
    }
    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (b = 0U; b < 8U; b++) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((crc << 1) ^ poly);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint8_t driver_crc8_maxim(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00U;
    size_t i;
    uint8_t b;

    if (data == NULL) {
        return crc;
    }
    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (b = 0U; b < 8U; b++) {
            if ((crc & 0x01U) != 0U) {
                crc = (uint8_t)((crc >> 1) ^ 0x8CU);
            } else {
                crc = (uint8_t)(crc >> 1);
            }
        }
    }
    return crc;
}

uint16_t driver_crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    size_t i;
    uint8_t b;

    if (data == NULL) {
        return crc;
    }
    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (b = 0U; b < 8U; b++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}
