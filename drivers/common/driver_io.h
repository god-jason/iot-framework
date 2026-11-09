#ifndef DRIVER_IO_H
#define DRIVER_IO_H

/*
 * 驱动公共 I/O 层。
 *
 * 所有驱动只通过本层访问硬件，本层再调用 hal 的抽象接口，
 * 因此驱动代码不依赖任何 MCU / HAL，可在 simulator 与真实平台复用。
 */

#include "driver_types.h"

/* 延时、时基、临界区统一来自 hal/hal_mcu.h（hal_delay_ms/us、hal_tick_get、
 * hal_irq_disable/enable），驱动不得直接调用 HAL 或自行忙等。 */
#include "hal_mcu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- I2C ---------------- */

/* 原始读写（不携带寄存器地址） */
int driver_i2c_write(driver_i2c_t *bus, const uint8_t *data, size_t len);
int driver_i2c_read(driver_i2c_t *bus, uint8_t *data, size_t len);

/* 8 位寄存器访问（Repeated Start） */
int driver_i2c_write_reg(driver_i2c_t *bus, uint8_t reg, const uint8_t *data, size_t len);
int driver_i2c_read_reg(driver_i2c_t *bus, uint8_t reg, uint8_t *data, size_t len);

/* 16 位寄存器访问（EEPROM / FRAM 等） */
int driver_i2c_write_reg16(driver_i2c_t *bus, uint16_t reg, const uint8_t *data, size_t len);
int driver_i2c_read_reg16(driver_i2c_t *bus, uint16_t reg, uint8_t *data, size_t len);

/* 探测从机是否应答 */
int driver_i2c_probe(const driver_i2c_t *bus);

/* ---------------- SPI ---------------- */

int driver_spi_transfer(driver_spi_t *bus, const uint8_t *tx, uint8_t *rx, size_t len);
int driver_spi_write(driver_spi_t *bus, const uint8_t *data, size_t len);
int driver_spi_read(driver_spi_t *bus, uint8_t *data, size_t len);

void driver_spi_select(driver_spi_t *bus);
void driver_spi_deselect(driver_spi_t *bus);

/* 片选保持期间的一次完整读写（自动拉低/拉高 CS） */
int driver_spi_write_cs(driver_spi_t *bus, const uint8_t *data, size_t len);
int driver_spi_read_cs(driver_spi_t *bus, uint8_t *data, size_t len);

/* ---------------- GPIO ---------------- */

int driver_gpio_deinit(hal_pin_t pin);
int driver_gpio_output(hal_pin_t pin, hal_gpio_level_e initial);
int driver_gpio_input(hal_pin_t pin, hal_gpio_pull_e pull);
int driver_gpio_write(hal_pin_t pin, hal_gpio_level_e level);
int driver_gpio_toggle(hal_pin_t pin);
/* 返回 0/1 电平，负值为错误码 */
int driver_gpio_read(hal_pin_t pin);

/* ---------------- 时间 ---------------- */

void driver_delay_ms(uint32_t ms);
void driver_delay_us(uint32_t us);
uint32_t driver_tick_ms(void);

/* ---------------- 工具 ---------------- */

/* 通用 CRC8（poly 由调用方给出） */
uint8_t driver_crc8(const uint8_t *data, size_t len, uint8_t poly, uint8_t init);

/* Dallas/Maxim CRC8：poly=0x31, init=0x00, 反射算法 */
uint8_t driver_crc8_maxim(const uint8_t *data, size_t len);

/* CRC16/Modbus */
uint16_t driver_crc16_modbus(const uint8_t *data, size_t len);

static inline int16_t driver_be16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline int16_t driver_le16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[1] << 8) | (uint16_t)p[0]);
}

static inline int32_t driver_be24(const uint8_t *p)
{
    int32_t v = ((int32_t)p[0] << 16) | ((int32_t)p[1] << 8) | (int32_t)p[2];
    if ((v & 0x800000) != 0) {
        v |= (int32_t)0xFF000000;
    }
    return v;
}

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_IO_H */
