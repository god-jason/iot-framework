#ifndef ADXL375_H
#define ADXL375_H

/*
 * ADXL375 高 g 三轴加速度计驱动（I2C 0x53/0x1D 或 SPI）。
 *
 * 参考 libdriver 风格：
 *   adxl375_init / adxl375_deinit / adxl375_read / adxl375_read_raw
 *
 * 设计要点：
 *   - init 校验 DEVID(0x00，期望 0xE5)，量程/输出率写入后回读校验；
 *   - 量程固定 ±200g，灵敏度 20.5 LSB/g（≈49 mg/LSB），read 输出 g；
 *   - read_raw 输出原始 16 位补码计数（小端）；
 *   - 输出率（100~3200Hz）可配置；
 *   - read 前检查 INT_SOURCE 的 DATA_READY / OVERRUN 状态位。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 枚举值与 BW_RATE 的输出率编码一致 */
typedef enum {
    ADXL375_ODR_100HZ = 0x0A,
    ADXL375_ODR_200HZ = 0x0B,
    ADXL375_ODR_400HZ = 0x0C,
    ADXL375_ODR_800HZ = 0x0D,
    ADXL375_ODR_1600HZ = 0x0E,
    ADXL375_ODR_3200HZ = 0x0F
} adxl375_odr_e;

typedef struct {
    float x;
    float y;
    float z;
} adxl375_vec3_t;

typedef struct {
    adxl375_vec3_t accel_g;     /* 加速度，g（±200g 量程） */
    bool data_ready;            /* INT_SOURCE.DATA_READY */
    bool overrun;               /* INT_SOURCE.OVERRUN */
} adxl375_data_t;

typedef struct {
    driver_i2c_t i2c;
    driver_spi_t spi;
    bool use_spi;
    adxl375_odr_e odr;
    bool initialized;
} adxl375_handle_t;

/**
 * @brief  初始化 ADXL375
 * @param  handle   驱动句柄
 * @param  i2c      I2C 主机句柄，SPI 模式传 NULL
 * @param  spi      SPI 主机句柄，I2C 模式传 NULL
 * @param  cs       片选引脚，总线自带片选时传 0
 * @param  address  7 位从机地址，0 表示默认 0x53（SPI 模式忽略）
 * @param  odr      输出率
 * @return 0 成功，负值为 HAL 状态码
 */
int adxl375_init(adxl375_handle_t *handle, hal_i2c_t *i2c,
                 hal_spi_t *spi, hal_pin_t cs, uint16_t address,
                 adxl375_odr_e odr);

/** @brief 反初始化：进入待机模式 */
int adxl375_deinit(adxl375_handle_t *handle);

/** @brief 读取加速度（g）与状态位 */
int adxl375_read(adxl375_handle_t *handle, adxl375_data_t *data);

/** @brief 读取原始计数 [x, y, z] */
int adxl375_read_raw(adxl375_handle_t *handle, int16_t xyz[3]);

/** @brief 读取 INT_SOURCE 寄存器 */
int adxl375_get_status(adxl375_handle_t *handle, uint8_t *int_source);

#ifdef __cplusplus
}
#endif

#endif /* ADXL375_H */
