#ifndef MCP3421_H
#define MCP3421_H

/*
 * MCP3421 18 位单通道 Δ-Σ ADC 驱动（I2C，固定地址 0x68）。
 *
 * 支持 12/14/16/18 位分辨率与 ×1/×2/×4/×8 PGA，单次或连续转换模式。
 * 参考 libdriver 风格：mcp3421_init / mcp3421_read / mcp3421_set_gain ...
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 转换模式（对应 C[1:0]） */
typedef enum {
    MCP3421_MODE_ONESHOT = 0,
    MCP3421_MODE_CONTINUOUS
} mcp3421_mode_t;

/* 分辨率与内建 ×2 档（对应 S[2:0]） */
typedef enum {
    MCP3421_RES_12BIT_1X = 0,
    MCP3421_RES_14BIT_1X,
    MCP3421_RES_16BIT_1X,
    MCP3421_RES_18BIT_1X,
    MCP3421_RES_12BIT_2X,
    MCP3421_RES_14BIT_2X,
    MCP3421_RES_16BIT_2X,
    MCP3421_RES_18BIT_2X
} mcp3421_res_t;

/* PGA 增益（对应 G[1:0]） */
typedef enum {
    MCP3421_GAIN_1 = 0,
    MCP3421_GAIN_2,
    MCP3421_GAIN_4,
    MCP3421_GAIN_8
} mcp3421_gain_t;

typedef struct {
    driver_i2c_t i2c;
    mcp3421_res_t resolution;
    mcp3421_gain_t pga;
    mcp3421_mode_t mode;
    bool initialized;
} mcp3421_handle_t;

/**
 * @brief  初始化：探测器件、写入默认配置并回读校验可写位
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址（固定 0x68），传 0 使用默认值
 * @return 0 成功，负值为 port_status_t
 */
int mcp3421_init(mcp3421_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief 反初始化（不关闭总线） */
int mcp3421_deinit(mcp3421_handle_t *handle);

/** @brief 设置分辨率（写配置寄存器） */
int mcp3421_set_resolution(mcp3421_handle_t *handle, mcp3421_res_t resolution);

/** @brief 设置 PGA 增益（写配置寄存器） */
int mcp3421_set_gain(mcp3421_handle_t *handle, mcp3421_gain_t pga);

/** @brief 设置转换模式（写配置寄存器） */
int mcp3421_set_mode(mcp3421_handle_t *handle, mcp3421_mode_t mode);

/**
 * @brief  读取一次转换结果（单次模式下会先触发转换并按分辨率等待）
 * @param  raw  右对齐后的有符号原始码值（12/14/16/18 位）
 */
int mcp3421_read_raw(mcp3421_handle_t *handle, int32_t *raw);

/** @brief 读取换算后的电压（V，按当前分辨率与增益） */
int mcp3421_read_voltage(mcp3421_handle_t *handle, float *volts);

#ifdef __cplusplus
}
#endif

#endif /* MCP3421_H */
