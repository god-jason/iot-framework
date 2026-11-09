#ifndef MPU6500_H
#define MPU6500_H

/*
 * MPU6500 六轴 IMU 驱动（三轴加速度 + 三轴陀螺 + 片上温度）。
 * 总线：I2C（0x68/0x69）或 SPI（4 线，CS 由调用方给出）。
 *
 * 参考 libdriver 风格：
 *   mpu6500_init / mpu6500_deinit / mpu6500_read / mpu6500_read_raw
 *
 * 设计要点：
 *   - init 校验 WHO_AM_I（0x75，期望 0x70），SPI 模式下关闭 I2C 从口；
 *   - 量程写入后回读校验，作为写通路自检；
 *   - read 按当前量程灵敏度换算 g / °/s / °C，read_raw 返回原始计数；
 *   - 输出率（4~1000Hz）可配置。
 *
 * 用法（I2C）：
 *   mpu6500_init(&imu, &i2c1, NULL, 0, 0x68,
 *                MPU6500_ACCEL_RANGE_4G, MPU6500_GYRO_RANGE_500DPS);
 * 用法（SPI）：
 *   mpu6500_init(&imu, NULL, &spi1, HAL_PIN(0, 4), 0,
 *                MPU6500_ACCEL_RANGE_4G, MPU6500_GYRO_RANGE_500DPS);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MPU6500_ACCEL_RANGE_2G = 0,
    MPU6500_ACCEL_RANGE_4G,
    MPU6500_ACCEL_RANGE_8G,
    MPU6500_ACCEL_RANGE_16G,
    MPU6500_ACCEL_RANGE_COUNT
} mpu6500_accel_range_e;

typedef enum {
    MPU6500_GYRO_RANGE_250DPS = 0,
    MPU6500_GYRO_RANGE_500DPS,
    MPU6500_GYRO_RANGE_1000DPS,
    MPU6500_GYRO_RANGE_2000DPS,
    MPU6500_GYRO_RANGE_COUNT
} mpu6500_gyro_range_e;

typedef struct {
    float x;
    float y;
    float z;
} mpu6500_vec3_t;

typedef struct {
    mpu6500_vec3_t accel_g;
    mpu6500_vec3_t gyro_dps;
    float temperature_c;
} mpu6500_data_t;

typedef struct {
    driver_i2c_t i2c;
    driver_spi_t spi;
    bool use_spi;
    mpu6500_accel_range_e accel_range;
    mpu6500_gyro_range_e gyro_range;
    float accel_lsb_per_g;
    float gyro_lsb_per_dps;
    bool initialized;
} mpu6500_handle_t;

/**
 * @brief  初始化 MPU6500
 * @param  handle       驱动句柄
 * @param  i2c          I2C 主机句柄，SPI 模式传 NULL
 * @param  spi          SPI 主机句柄，I2C 模式传 NULL
 * @param  cs           片选引脚，总线自带片选时传 0
 * @param  address      7 位从机地址，0 表示默认 0x68（SPI 模式忽略）
 * @param  accel_range  加速度量程
 * @param  gyro_range   陀螺量程
 * @return 0 成功，负值为 HAL 状态码
 */
int mpu6500_init(mpu6500_handle_t *handle, hal_i2c_t *i2c,
                 hal_spi_t *spi, hal_pin_t cs, uint16_t address,
                 mpu6500_accel_range_e accel_range, mpu6500_gyro_range_e gyro_range);

/** @brief 反初始化：进入睡眠并复位句柄状态 */
int mpu6500_deinit(mpu6500_handle_t *handle);

/** @brief 读取物理量：g / °/s / °C */
int mpu6500_read(mpu6500_handle_t *handle, mpu6500_data_t *data);

/** @brief 读取原始计数（温度可为 NULL） */
int mpu6500_read_raw(mpu6500_handle_t *handle, int16_t accel[3], int16_t gyro[3],
                     int16_t *temperature_raw);

/** @brief 配置输出率（4~1000 Hz） */
int mpu6500_set_sample_rate(mpu6500_handle_t *handle, uint16_t rate_hz);

/** @brief 读取中断状态寄存器（bit0 为数据就绪） */
int mpu6500_get_status(mpu6500_handle_t *handle, uint8_t *int_status);

#ifdef __cplusplus
}
#endif

#endif /* MPU6500_H */
