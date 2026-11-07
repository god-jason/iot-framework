#ifndef VEML3328_H
#define VEML3328_H

/*
 * VEML3328 RGB/IR 颜色传感器驱动（Vishay，I2C 7 位地址 0x10）。
 *
 * 参考 libdriver 风格：
 *   veml3328_init / veml3328_deinit / veml3328_read / veml3328_read_raw
 * 所有硬件访问只经由 drivers/common/driver_io.h。
 *
 * 设计要点：
 *   - 5 个通道：Clear / Red / Green / Blue / IR，各 16 位；
 *   - 固定配置：IT=100ms、差分增益 1:1、增益 1:1、关闭自动量程；
 *   - read() 额外给出照度（lux）近似值，系数见 veml3328_register.h 的说明。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VEML3328_CHANNEL_CLEAR = 0,
    VEML3328_CHANNEL_RED,
    VEML3328_CHANNEL_GREEN,
    VEML3328_CHANNEL_BLUE,
    VEML3328_CHANNEL_IR,
    VEML3328_CHANNEL_COUNT
} veml3328_channel_t;

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} veml3328_handle_t;

typedef struct {
    uint16_t clear;         /* 清通道（无色）计数 */
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    uint16_t ir;            /* 红外通道计数，通常远大于 RGB 时为白炽/阳光环境 */
    float lux;              /* 照度近似值，单位 lux，未标定 */
} veml3328_data_t;

/**
 * @brief  初始化 VEML3328（probe + 写 CONF 并回读校验 + 首次转换等待）
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示默认 0x10
 * @return 0 成功，负值为 iot_status_t
 */
int veml3328_init(veml3328_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/** @brief 反初始化：关断传感器（CONF.SD = 1） */
int veml3328_deinit(veml3328_handle_t *handle);

/** @brief 读取 RGBC + IR 计数值与照度近似值 */
int veml3328_read(veml3328_handle_t *handle, veml3328_data_t *data);

/**
 * @brief  读取 5 通道原始计数
 * @param  counts  长度为 VEML3328_CHANNEL_COUNT，顺序 C/R/G/B/IR
 */
int veml3328_read_raw(veml3328_handle_t *handle, uint16_t counts[VEML3328_CHANNEL_COUNT]);

/** @brief 读取 CONF 寄存器（便于上层判断量程/积分时间是否饱和） */
int veml3328_get_conf(veml3328_handle_t *handle, uint16_t *conf);

#ifdef __cplusplus
}
#endif

#endif /* VEML3328_H */
