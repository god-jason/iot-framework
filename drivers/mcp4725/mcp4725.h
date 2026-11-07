#ifndef MCP4725_H
#define MCP4725_H

/*
 * MCP4725 12 位电压输出 DAC 驱动（I2C，内置 EEPROM）。
 *
 * 参考 libdriver 风格：mcp4725_init / mcp4725_set_voltage / mcp4725_write ...
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 掉电模式（对应命令字节 PD[1:0]） */
typedef enum {
    MCP4725_PD_NORMAL = 0,      /* 正常输出 */
    MCP4725_PD_1K_TO_GND,       /* 1kΩ 下拉到地 */
    MCP4725_PD_100K_TO_GND,     /* 100kΩ 下拉到地 */
    MCP4725_PD_500K_TO_GND      /* 500kΩ 下拉到地 */
} mcp4725_pd_t;

typedef struct {
    driver_i2c_t i2c;
    uint16_t vref_mv;           /* 参考电压（VDD），用于电压换算 */
    uint16_t dac_value;         /* 最近写入的码值（影子寄存器） */
    bool initialized;
} mcp4725_handle_t;

/**
 * @brief  初始化：探测器件、读取当前 DAC/EEPROM 内容并按 EEPROM 值输出
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址（0x60~0x67），传 0 使用 0x60
 * @return 0 成功，负值为 port_status_t
 */
int mcp4725_init(mcp4725_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线，输出保持） */
int mcp4725_deinit(mcp4725_handle_t *handle);

/** @brief 设置参考电压（mV），用于 mcp4725_set_voltage 换算 */
int mcp4725_set_vref(mcp4725_handle_t *handle, uint16_t vref_mv);

/** @brief 快速写：直接输出 12 位码值，不写 EEPROM */
int mcp4725_write(mcp4725_handle_t *handle, uint16_t value);

/** @brief 写 DAC 寄存器并指定掉电模式（命令 0100） */
int mcp4725_write_dac(mcp4725_handle_t *handle, uint16_t value, mcp4725_pd_t pd);

/** @brief 写 DAC 与 EEPROM（命令 0110，掉电后保持），内部等待 50ms */
int mcp4725_save(mcp4725_handle_t *handle, uint16_t value, mcp4725_pd_t pd);

/** @brief 按码值输出，等价于 mcp4725_write */
int mcp4725_set_raw(mcp4725_handle_t *handle, uint16_t value);

/** @brief 按电压输出（V），按 vref 换算并限幅 */
int mcp4725_set_voltage(mcp4725_handle_t *handle, float volts);

/**
 * @brief  读回当前 DAC 码值与掉电模式
 * @param  value  可为 NULL
 * @param  pd     可为 NULL
 */
int mcp4725_read(mcp4725_handle_t *handle, uint16_t *value, mcp4725_pd_t *pd);

#ifdef __cplusplus
}
#endif

#endif /* MCP4725_H */
