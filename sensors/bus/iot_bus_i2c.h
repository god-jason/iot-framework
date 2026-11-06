#ifndef IOT_BUS_I2C_H
#define IOT_BUS_I2C_H

/*
 * 通用 I2C 总线抽象：所有 I2C 传感器统一通过本接口读写，
 * 便于在 host 用 mock 测试、在 MCU 用 HAL 实现。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*read)(void *ctx, uint8_t dev_addr, uint8_t *data, uint32_t len);
    int (*write)(void *ctx, uint8_t dev_addr, const uint8_t *data, uint32_t len);
    void *ctx;
} iot_i2c_bus_t;

/* 写单个寄存器 */
int iot_i2c_write_reg(const iot_i2c_bus_t *bus, uint8_t dev, uint8_t reg, uint8_t val);

/* 写多个寄存器（reg 后跟 data） */
int iot_i2c_write_regs(const iot_i2c_bus_t *bus, uint8_t dev, uint8_t reg,
                       const uint8_t *data, uint32_t len);

/* 读单个寄存器 */
int iot_i2c_read_reg(const iot_i2c_bus_t *bus, uint8_t dev, uint8_t reg, uint8_t *val);

/* 读多个连续寄存器 */
int iot_i2c_read_regs(const iot_i2c_bus_t *bus, uint8_t dev, uint8_t reg,
                      uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* IOT_BUS_I2C_H */