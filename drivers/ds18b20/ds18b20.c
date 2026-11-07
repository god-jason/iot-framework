#include "ds18b20.h"
#include "ds18b20_register.h"
#include "log.h"

static void ds18b20_low(ds18b20_handle_t *handle)
{
    (void)port_gpio_write(handle->pin, PORT_GPIO_LOW);
}

static void ds18b20_release(ds18b20_handle_t *handle)
{
    (void)port_gpio_write(handle->pin, PORT_GPIO_HIGH);
}

static int ds18b20_sample(ds18b20_handle_t *handle)
{
    port_gpio_level_t level = PORT_GPIO_LOW;

    if (port_gpio_read(handle->pin, &level) != PORT_OK) {
        return 0;
    }
    return (level == PORT_GPIO_HIGH) ? 1 : 0;
}

static void ds18b20_write_bit(ds18b20_handle_t *handle, uint8_t bit)
{
    port_irq_disable();
    ds18b20_low(handle);
    driver_delay_us(DS18B20_WRITE_LOW_US);
    if (bit != 0U) {
        ds18b20_release(handle);
    }
    driver_delay_us(DS18B20_WRITE_HOLD_US);
    ds18b20_release(handle);
    driver_delay_us(DS18B20_WRITE_LOW_US);
    port_irq_enable();
}

static uint8_t ds18b20_read_bit(ds18b20_handle_t *handle)
{
    uint8_t bit;

    port_irq_disable();
    ds18b20_low(handle);
    driver_delay_us(DS18B20_READ_LOW_US);
    ds18b20_release(handle);
    driver_delay_us(DS18B20_READ_SAMPLE_US);
    bit = (uint8_t)ds18b20_sample(handle);
    driver_delay_us(DS18B20_READ_TAIL_US);
    port_irq_enable();
    return bit;
}

static void ds18b20_write_byte(ds18b20_handle_t *handle, uint8_t value)
{
    uint8_t i;

    for (i = 0U; i < 8U; i++) {
        ds18b20_write_bit(handle, (uint8_t)((value >> i) & 0x01U));
    }
}

static uint8_t ds18b20_read_byte(ds18b20_handle_t *handle)
{
    uint8_t i;
    uint8_t value = 0U;

    for (i = 0U; i < 8U; i++) {
        if (ds18b20_read_bit(handle) != 0U) {
            value = (uint8_t)(value | DRIVER_BIT(i));
        }
    }
    return value;
}

int ds18b20_init(ds18b20_handle_t *handle, port_pin_t pin)
{
    port_gpio_config_t cfg;

    if (handle == NULL || pin == 0U) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->pin = pin;
    handle->initialized = false;

    /* 开漏输出 + 上拉：写 1 释放总线，同时可直接读取引脚电平 */
    cfg.mode = PORT_GPIO_MODE_OUTPUT;
    cfg.output = PORT_GPIO_OUTPUT_OD;
    cfg.pull = PORT_GPIO_PULLUP;
    cfg.speed = PORT_GPIO_SPEED_HIGH;
    cfg.alternate = 0U;
    cfg.initial_level = PORT_GPIO_HIGH;

    if (port_gpio_init(pin, &cfg) != PORT_OK) {
        LOG_E("ds18b20", "gpio init failed");
        return (int)PORT_IO_ERROR;
    }

    handle->initialized = true;
    if (ds18b20_reset(handle) != 1) {
        LOG_W("ds18b20", "no device on bus");
    }
    return DRIVER_OK;
}

int ds18b20_deinit(ds18b20_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->pin != 0U) {
        (void)port_gpio_deinit(handle->pin);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int ds18b20_reset(ds18b20_handle_t *handle)
{
    int presence;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    port_irq_disable();
    ds18b20_low(handle);
    driver_delay_us(DS18B20_RESET_LOW_US);
    ds18b20_release(handle);
    driver_delay_us(DS18B20_RESET_RELEASE_US);
    presence = ds18b20_sample(handle);
    driver_delay_us(DS18B20_RESET_WAIT_US);
    port_irq_enable();

    return (presence == 0) ? 1 : 0;
}

int ds18b20_start(ds18b20_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (ds18b20_reset(handle) != 1) {
        return (int)PORT_NOT_FOUND;
    }
    ds18b20_write_byte(handle, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(handle, DS18B20_CMD_CONVERT_T);
    return DRIVER_OK;
}

int ds18b20_read_scratchpad(ds18b20_handle_t *handle, uint8_t scratchpad[9])
{
    uint8_t i;

    if (handle == NULL || scratchpad == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (ds18b20_reset(handle) != 1) {
        return (int)PORT_NOT_FOUND;
    }
    ds18b20_write_byte(handle, DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(handle, DS18B20_CMD_READ_SCRATCH);
    for (i = 0U; i < DS18B20_SCRATCHPAD_SIZE; i++) {
        scratchpad[i] = ds18b20_read_byte(handle);
    }

    if (driver_crc8_maxim(scratchpad, DS18B20_SCRATCHPAD_SIZE - 1U) != scratchpad[8]) {
        LOG_E("ds18b20", "crc error");
        return (int)PORT_CRC_ERROR;
    }
    return DRIVER_OK;
}

int ds18b20_read(ds18b20_handle_t *handle, float *celsius)
{
    uint8_t scratchpad[DS18B20_SCRATCHPAD_SIZE];
    int16_t raw;
    int ret;

    if (celsius == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = ds18b20_start(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(DS18B20_CONVERT_DELAY_MS);

    ret = ds18b20_read_scratchpad(handle, scratchpad);
    if (ret != DRIVER_OK) {
        return ret;
    }

    raw = (int16_t)((uint16_t)scratchpad[0] | ((uint16_t)scratchpad[1] << 8));
    *celsius = (float)raw * DS18B20_RESOLUTION_LSB;
    return DRIVER_OK;
}
