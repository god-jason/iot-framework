#include <string.h>
#include "ds2431.h"
#include "ds2431_register.h"
#include "log.h"

#define DS2431_CHECK(h) \
    do { if ((h) == NULL || !(h)->initialized) { return (int)PORT_INVALID_PARAM; } } while (0)

static void ds2431_low(ds2431_handle_t *h) { (void)port_gpio_write(h->pin, PORT_GPIO_LOW); }
static void ds2431_release(ds2431_handle_t *h) { (void)port_gpio_write(h->pin, PORT_GPIO_HIGH); }

static int ds2431_sample(ds2431_handle_t *h)
{
    port_gpio_level_t level = PORT_GPIO_LOW;
    if (port_gpio_read(h->pin, &level) != PORT_OK) { return 0; }
    return (level == PORT_GPIO_HIGH) ? 1 : 0;
}

/* LSB first，写 1 为短低电平，写 0 为长低电平 */
static void ds2431_write_byte(ds2431_handle_t *h, uint8_t value)
{
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
        port_irq_disable();
        ds2431_low(h);
        if ((value & 0x01U) != 0U) {
            driver_delay_us(DS2431_WRITE1_LOW_US);
            ds2431_release(h);
            driver_delay_us(DS2431_WRITE0_LOW_US);
        } else {
            driver_delay_us(DS2431_WRITE0_LOW_US);
            ds2431_release(h);
            driver_delay_us(DS2431_WRITE_RECOVERY_US);
        }
        port_irq_enable();
        value = (uint8_t)(value >> 1);
    }
}

static uint8_t ds2431_read_byte(ds2431_handle_t *h)
{
    uint8_t i;
    uint8_t value = 0U;
    for (i = 0U; i < 8U; i++) {
        port_irq_disable();
        ds2431_low(h);
        driver_delay_us(DS2431_READ_INIT_US);
        ds2431_release(h);
        driver_delay_us(DS2431_READ_SAMPLE_US);
        if (ds2431_sample(h) != 0) { value |= (uint8_t)(1U << i); }
        driver_delay_us(DS2431_READ_TAIL_US);
        port_irq_enable();
    }
    return value;
}

/* DS2431 使用反射 CRC16（poly 0xA001，初值 0，结果取反） */
static uint16_t ds2431_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0U;
    size_t i;
    uint8_t b;
    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (b = 0U; b < 8U; b++) {
            crc = ((crc & 0x0001U) != 0U) ? (uint16_t)((crc >> 1) ^ 0xA001U)
                                          : (uint16_t)(crc >> 1);
        }
    }
    return (uint16_t)~crc;
}

int ds2431_reset(ds2431_handle_t *h)
{
    int presence;
    if (h == NULL || h->pin == 0U) { return (int)PORT_INVALID_PARAM; }
    port_irq_disable();
    ds2431_low(h);
    driver_delay_us(DS2431_RESET_LOW_US);
    ds2431_release(h);
    driver_delay_us(DS2431_RESET_RELEASE_US);
    presence = ds2431_sample(h);
    driver_delay_us(DS2431_PRESENCE_WAIT_US);
    port_irq_enable();
    return (presence == 0) ? 1 : 0;
}

int ds2431_read_rom(ds2431_handle_t *h, uint8_t rom[8])
{
    uint8_t i;
    DS2431_CHECK(h);
    if (rom == NULL) { return (int)PORT_INVALID_PARAM; }
    if (ds2431_reset(h) != 1) { return (int)PORT_NOT_FOUND; }
    ds2431_write_byte(h, DS2431_CMD_READ_ROM);
    for (i = 0U; i < DS2431_ROM_SIZE; i++) { rom[i] = ds2431_read_byte(h); }
    if (driver_crc8_maxim(rom, DS2431_ROM_SIZE - 1U) != rom[DS2431_ROM_SIZE - 1U]) {
        return (int)PORT_CRC_ERROR;
    }
    if (rom[0] != 0x2DU) {
        LOG_E("ds2431", "unexpected family 0x%02X", rom[0]);
        return (int)PORT_NOT_FOUND;
    }
    (void)memcpy(h->rom, rom, DS2431_ROM_SIZE);
    h->rom_valid = true;
    return DRIVER_OK;
}

int ds2431_read(ds2431_handle_t *h, uint8_t address, uint8_t *data, size_t len)
{
    size_t i;
    DS2431_CHECK(h);
    if (data == NULL || len == 0U) { return (int)PORT_INVALID_PARAM; }
    if (address >= DS2431_MEM_SIZE || len > (size_t)(DS2431_MEM_SIZE - address)) {
        return (int)PORT_OVERFLOW;
    }
    if (ds2431_reset(h) != 1) { return (int)PORT_NOT_FOUND; }
    ds2431_write_byte(h, DS2431_CMD_SKIP_ROM);
    ds2431_write_byte(h, DS2431_CMD_READ_MEMORY);
    ds2431_write_byte(h, (uint8_t)(address & 0xFFU));
    ds2431_write_byte(h, (uint8_t)((address >> 8) & 0xFFU));
    for (i = 0U; i < len; i++) { data[i] = ds2431_read_byte(h); }
    return DRIVER_OK;
}

int ds2431_write(ds2431_handle_t *h, uint8_t address, const uint8_t *data, size_t len)
{
    uint8_t sp[DS2431_SCRATCHPAD_SIZE];
    uint8_t verify[DS2431_SCRATCH_DATA];
    uint8_t end_addr;
    uint16_t crc;
    size_t i;

    DS2431_CHECK(h);
    if (data == NULL || len == 0U || len > DS2431_SCRATCH_DATA) {
        return (int)PORT_INVALID_PARAM;
    }
    if (address >= DS2431_MEM_SIZE || len > (size_t)(DS2431_MEM_SIZE - address)) {
        return (int)PORT_OVERFLOW;
    }
    if ((size_t)(address & DS2431_PAGE_MASK) + len > DS2431_PAGE_SIZE) {
        return (int)PORT_INVALID_PARAM;   /* 不允许跨页 */
    }
    end_addr = (uint8_t)((address & DS2431_PAGE_MASK) + len - 1U);

    if (ds2431_reset(h) != 1) { return (int)PORT_NOT_FOUND; }
    ds2431_write_byte(h, DS2431_CMD_SKIP_ROM);
    ds2431_write_byte(h, DS2431_CMD_WRITE_SCRATCH);
    ds2431_write_byte(h, (uint8_t)(address & 0xFFU));
    ds2431_write_byte(h, (uint8_t)((address >> 8) & 0xFFU));
    for (i = 0U; i < len; i++) { ds2431_write_byte(h, data[i]); }

    /* 回读 scratchpad：TA1/TA2/ES + 数据 + 反相 CRC16 */
    if (ds2431_reset(h) != 1) { return (int)PORT_NOT_FOUND; }
    ds2431_write_byte(h, DS2431_CMD_SKIP_ROM);
    ds2431_write_byte(h, DS2431_CMD_READ_SCRATCH);
    for (i = 0U; i < DS2431_SCRATCHPAD_SIZE; i++) { sp[i] = ds2431_read_byte(h); }
    if (sp[0] != (uint8_t)(address & 0xFFU) || sp[1] != (uint8_t)(address >> 8) ||
        sp[2] != end_addr) {
        LOG_E("ds2431", "scratchpad address mismatch");
        return (int)PORT_IO_ERROR;
    }
    crc = (uint16_t)((uint16_t)sp[11] | ((uint16_t)sp[12] << 8));
    if (ds2431_crc16(sp, 11U) != crc) {
        LOG_E("ds2431", "scratchpad crc error");
        return (int)PORT_CRC_ERROR;
    }

    /* Copy Scratchpad（tPROG 期间需要强上拉，此处依赖板上上拉 + 延时） */
    if (ds2431_reset(h) != 1) { return (int)PORT_NOT_FOUND; }
    ds2431_write_byte(h, DS2431_CMD_SKIP_ROM);
    ds2431_write_byte(h, DS2431_CMD_COPY_SCRATCH);
    ds2431_write_byte(h, sp[0]);
    ds2431_write_byte(h, sp[1]);
    ds2431_write_byte(h, sp[2]);
    driver_delay_ms(DS2431_COPY_DELAY_MS);

    if (ds2431_read(h, address, verify, len) != DRIVER_OK) { return (int)PORT_IO_ERROR; }
    if (memcmp(verify, data, len) != 0) {
        LOG_E("ds2431", "verify failed");
        return (int)PORT_CRC_ERROR;
    }
    return DRIVER_OK;
}

int ds2431_init(ds2431_handle_t *handle, port_pin_t pin)
{
    port_gpio_config_t cfg;
    if (handle == NULL || pin == 0U) { return (int)PORT_INVALID_PARAM; }
    (void)memset(handle, 0, sizeof(*handle));
    handle->pin = pin;
    cfg.mode = PORT_GPIO_MODE_OUTPUT;
    cfg.output = PORT_GPIO_OUTPUT_OD;
    cfg.pull = PORT_GPIO_PULLUP;
    cfg.speed = PORT_GPIO_SPEED_HIGH;
    cfg.alternate = 0U;
    cfg.initial_level = PORT_GPIO_HIGH;
    if (port_gpio_init(pin, &cfg) != PORT_OK) {
        LOG_E("ds2431", "gpio init failed");
        return (int)PORT_IO_ERROR;
    }
    handle->initialized = true;
    if (ds2431_reset(handle) != 1) {
        LOG_E("ds2431", "no device on bus");
        handle->initialized = false;
        return (int)PORT_NOT_FOUND;
    }
    if (ds2431_read_rom(handle, handle->rom) != DRIVER_OK) {
        handle->initialized = false;
        return (int)PORT_NOT_FOUND;
    }
    LOG_I("ds2431", "init ok, rom %02X%02X%02X%02X%02X%02X%02X%02X",
          handle->rom[0], handle->rom[1], handle->rom[2], handle->rom[3],
          handle->rom[4], handle->rom[5], handle->rom[6], handle->rom[7]);
    return DRIVER_OK;
}

int ds2431_deinit(ds2431_handle_t *handle)
{
    if (handle == NULL) { return (int)PORT_INVALID_PARAM; }
    if (handle->pin != 0U) { (void)port_gpio_deinit(handle->pin); }
    handle->initialized = false;
    return DRIVER_OK;
}
