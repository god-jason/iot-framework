#ifndef BME680_H
#define BME680_H

/*
 * BME680 温度 / 湿度 / 气压（+气体）传感器驱动（I2C）。
 *
 * 参考 libdriver 风格：
 *   bme680_init / bme680_deinit / bme680_read / bme680_read_raw
 *
 * 初始化时校验 chip id（0x61）并读取 0x89 与 0xE1 两组校准系数；
 * 每次读取触发一次 forced 模式测量，按 datasheet 双精度公式补偿温湿压。
 *
 * 已知限制：气体电阻需要加热器（res_heat / gas_wait）标定与气体补偿算法，
 * 本驱动未实现，data->gas_resistance 恒为 0，且不启动气体测量。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t t1;
    int16_t  t2;
    int8_t   t3;

    uint16_t p1;
    int16_t  p2;
    int8_t   p3;
    int16_t  p4;
    int16_t  p5;
    int8_t   p6;
    int8_t   p7;
    int16_t  p8;
    int16_t  p9;
    uint8_t  p10;

    uint16_t h1;     /* 0xE2 低 4 位 + 0xE3 */
    int16_t  h2;     /* 0xE1 + 0xE2 高 4 位，12 位补码 */
    int8_t   h3;
    int8_t   h4;
    int8_t   h5;
    int8_t   h6;
    int8_t   h7;
} bme680_calib_t;

typedef struct {
    driver_i2c_t i2c;
    bme680_calib_t calib;
    double t_fine;
    bool initialized;
} bme680_handle_t;

typedef struct {
    float temperature;      /* ℃ */
    float pressure;         /* Pa */
    float humidity;         /* %RH */
    float gas_resistance;   /* Ω，保留字段：未实现气体测量，恒为 0 */
} bme680_data_t;

/** 初始化：校验 chip id 0x61，读两组校准系数，关闭气体通道 */
int bme680_init(bme680_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/** 反初始化（进入 sleep，不关闭总线） */
int bme680_deinit(bme680_handle_t *handle);

/** 触发一次 forced 测量并读取温度 ℃ / 气压 Pa / 湿度 %RH */
int bme680_read(bme680_handle_t *handle, bme680_data_t *data);

/** 读取原始 ADC 值（20 位温压、16 位湿度），任一指针可为 NULL */
int bme680_read_raw(bme680_handle_t *handle, int32_t *raw_temperature,
                    int32_t *raw_pressure, int32_t *raw_humidity);

/** 读取芯片 ID，正常为 0x61 */
int bme680_get_chip_id(bme680_handle_t *handle, uint8_t *chip_id);

#ifdef __cplusplus
}
#endif

#endif /* BME680_H */
