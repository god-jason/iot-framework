#ifndef BME280_H
#define BME280_H

/*
 * BME280 温度 / 湿度 / 气压传感器驱动（I2C）。
 *
 * 参考 libdriver 风格：
 *   bme280_init / bme280_deinit / bme280_read / bme280_read_raw
 *
 * 初始化时读取 0x88（温压 26 字节）与 0xE1（湿度 7 字节）两组校准系数，
 * 读取时按 datasheet 公式补偿：温度/压力使用 32 位定点算法，
 * 湿度使用双精度算法（H4/H5 为 12 位补码，需符号扩展）。支持多实例。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t t1;
    int16_t  t2;
    int16_t  t3;

    uint16_t p1;
    int16_t  p2;
    int16_t  p3;
    int16_t  p4;
    int16_t  p5;
    int16_t  p6;
    int16_t  p7;
    int16_t  p8;
    int16_t  p9;

    uint8_t  h1;
    int16_t  h2;      /* 0xE1/0xE2 组合，12 位补码 */
    uint8_t  h3;
    int16_t  h4;      /* 0xE4 + 0xE5 低 4 位，12 位补码 */
    int16_t  h5;      /* 0xE6 + 0xE5 高 4 位，12 位补码 */
    int8_t   h6;
} bme280_calib_t;

typedef struct {
    driver_i2c_t i2c;
    bme280_calib_t calib;
    int32_t t_fine;
    bool initialized;
} bme280_handle_t;

typedef struct {
    float temperature;   /* ℃ */
    float pressure;      /* Pa */
    float humidity;      /* %RH */
} bme280_data_t;

/** 初始化：校验 chip id 0x60，读两组校准系数并配置为 normal 模式 */
int bme280_init(bme280_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** 反初始化（进入 sleep，不关闭总线） */
int bme280_deinit(bme280_handle_t *handle);

/** 读取一次温度 ℃ / 气压 Pa / 湿度 %RH */
int bme280_read(bme280_handle_t *handle, bme280_data_t *data);

/** 读取原始 ADC 值，任一指针可为 NULL */
int bme280_read_raw(bme280_handle_t *handle, int32_t *raw_temperature,
                    int32_t *raw_pressure, int32_t *raw_humidity);

/** 读取芯片 ID，正常为 0x60 */
int bme280_get_chip_id(bme280_handle_t *handle, uint8_t *chip_id);

#ifdef __cplusplus
}
#endif

#endif /* BME280_H */
