#ifndef BA111_H
#define BA111_H

/*
 * BA111 I2C 温度传感器驱动。
 *
 * 命令集未公开确认：本驱动按通用布局实现基础读写（0x00 温度寄存器，
 * 16 位有符号，1 LSB = 0.0625 ℃），并提供原始读数与通用寄存器读写，
 * 便于按实际器件手册调整。详见 ba111_register.h 顶部说明。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} ba111_handle_t;

/** @brief  初始化并探测从机，address 为 0 时使用 0x48 */
int ba111_init(ba111_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief  反初始化（不关闭总线） */
int ba111_deinit(ba111_handle_t *handle);

/** @brief  读取温度（℃） */
int ba111_read(ba111_handle_t *handle, float *celsius);

/** @brief  读取原始温度码 */
int ba111_read_raw(ba111_handle_t *handle, int16_t *raw);

/** @brief  读取任意寄存器 */
int ba111_read_reg(ba111_handle_t *handle, uint8_t reg, uint8_t *data, size_t len);

/** @brief  写任意寄存器 */
int ba111_write_reg(ba111_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BA111_H */
