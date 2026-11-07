#ifndef MCP9600_H
#define MCP9600_H

/*
 * MCP9600 热电偶温度转换器驱动（I2C，7 位地址 0x60 - 0x67，默认 0x60）。
 *
 * 支持 K/J/T/N/E/B/R/S 型热电偶，16 位有符号寄存器，
 * 热端 / 冷端 / 温差均为 0.0625 ℃/LSB，量程与精度依热电偶类型而定。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} mcp9600_handle_t;

/** @brief  初始化并探测从机，address 为 0 时使用 0x60 */
int mcp9600_init(mcp9600_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/** @brief  反初始化（不关闭总线） */
int mcp9600_deinit(mcp9600_handle_t *handle);

/** @brief  读取热端（热电偶）温度（℃） */
int mcp9600_read(mcp9600_handle_t *handle, float *celsius);

/** @brief  读取冷端（环境）温度（℃） */
int mcp9600_read_cold_junction(mcp9600_handle_t *handle, float *celsius);

/** @brief  读取冷热端温差（℃） */
int mcp9600_read_delta(mcp9600_handle_t *handle, float *celsius);

/**
 * @brief  设置热电偶类型
 * @param  type  MCP9600_TC_TYPE_K / J / T / N / E / B / R / S
 */
int mcp9600_set_thermocouple_type(mcp9600_handle_t *handle, uint8_t type);

/**
 * @brief  设置数字滤波系数（0 - 7，越大越平滑、响应越慢）
 */
int mcp9600_set_filter(mcp9600_handle_t *handle, uint8_t coefficient);

/** @brief  读取器件 ID（应为 0x40） */
int mcp9600_get_device_id(mcp9600_handle_t *handle, uint8_t *id);

/** @brief  直接读寄存器 */
int mcp9600_read_reg(mcp9600_handle_t *handle, uint8_t reg, uint8_t *data, size_t len);

/** @brief  直接写寄存器 */
int mcp9600_write_reg(mcp9600_handle_t *handle, uint8_t reg, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MCP9600_H */
