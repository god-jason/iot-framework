#ifndef ADXL345_H
#define ADXL345_H

/*
 * ADXL345 三轴加速度计驱动（I2C）。
 *
 * 采用全分辨率模式，灵敏度固定 3.9 mg/LSB。
 * 参考 libdriver 风格：adxl345_init / adxl345_read / adxl345_read_raw。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ADXL345_RANGE_2G = 0,
    ADXL345_RANGE_4G,
    ADXL345_RANGE_8G,
    ADXL345_RANGE_16G
} adxl345_range_t;

typedef struct {
    driver_i2c_t i2c;
    adxl345_range_t range;
    bool initialized;
} adxl345_handle_t;

typedef struct {
    float x;
    float y;
    float z;
} adxl345_accel_t;

int adxl345_init(adxl345_handle_t *handle, port_i2c_handle_t *i2c,
                 uint16_t address, adxl345_range_t range);
int adxl345_deinit(adxl345_handle_t *handle);

int adxl345_read_raw(adxl345_handle_t *handle, int16_t xyz[3]);
int adxl345_read(adxl345_handle_t *handle, adxl345_accel_t *accel);

int adxl345_set_measure(adxl345_handle_t *handle, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* ADXL345_H */
