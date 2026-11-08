#ifndef L3GD20H_H
#define L3GD20H_H

/*
 * L3GD20H 三轴陀螺仪驱动（I2C 0x6A/0x6B 或 SPI）。
 *
 * 参考 libdriver 风格：
 *   l3gd20h_init / l3gd20h_deinit / l3gd20h_read / l3gd20h_read_raw
 *
 * 设计要点：
 *   - init 校验 WHO_AM_I(0x0F，期望 0xD7)，量程位回读校验；
 *   - read 把原始 LSB 按当前量程灵敏度（8.75/17.5/70 mdps/LSB）换算为 °/s；
 *   - read_raw 输出原始 16 位补码计数（小端）；
 *   - 量程与输出率（95/190/380/760 Hz）可配置。
 *
 * 注意：片上温度传感器精度约 ±15 °C，仅用于粗略趋势参考。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    L3GD20H_RANGE_245DPS = 0,
    L3GD20H_RANGE_500DPS,
    L3GD20H_RANGE_2000DPS,
    L3GD20H_RANGE_COUNT
} l3gd20h_range_t;

typedef enum {
    L3GD20H_ODR_95HZ = 0,
    L3GD20H_ODR_190HZ,
    L3GD20H_ODR_380HZ,
    L3GD20H_ODR_760HZ,
    L3GD20H_ODR_COUNT
} l3gd20h_odr_t;

typedef struct {
    float x;
    float y;
    float z;
} l3gd20h_vec3_t;

typedef struct {
    l3gd20h_vec3_t dps;         /* 角速度，°/s */
    float temperature_c;        /* 片上温度，°C（精度有限） */
} l3gd20h_data_t;

typedef struct {
    driver_i2c_t i2c;
    driver_spi_t spi;
    bool use_spi;
    l3gd20h_range_t range;
    l3gd20h_odr_t odr;
    float mdps_per_lsb;         /* 当前量程灵敏度 */
    bool initialized;
} l3gd20h_handle_t;

/**
 * @brief  初始化 L3GD20H
 * @param  handle   驱动句柄
 * @param  i2c      I2C 主机句柄，SPI 模式传 NULL
 * @param  spi      SPI 主机句柄，I2C 模式传 NULL
 * @param  cs       片选引脚，总线自带片选时传 0
 * @param  address  7 位从机地址，0 表示默认 0x6A（SPI 模式忽略）
 * @param  range    量程
 * @param  odr      输出率
 * @return 0 成功，负值为 hal_status_t
 */
int l3gd20h_init(l3gd20h_handle_t *handle, hal_i2c_t *i2c,
                 hal_spi_t *spi, hal_pin_t cs, uint16_t address,
                 l3gd20h_range_t range, l3gd20h_odr_t odr);

/** @brief 反初始化：进入掉电模式 */
int l3gd20h_deinit(l3gd20h_handle_t *handle);

/** @brief 读取角速度（°/s）与片上温度 */
int l3gd20h_read(l3gd20h_handle_t *handle, l3gd20h_data_t *data);

/** @brief 读取原始计数：[gx, gy, gz]，温度为 8 位补码，可传 NULL */
int l3gd20h_read_raw(l3gd20h_handle_t *handle, int16_t gyro[3], int16_t *temperature_raw);

/** @brief 读取 STATUS 寄存器（溢出 / 数据就绪标志） */
int l3gd20h_get_status(l3gd20h_handle_t *handle, uint8_t *status);

#ifdef __cplusplus
}
#endif

#endif /* L3GD20H_H */
