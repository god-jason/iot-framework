#ifndef MPU9250_H
#define MPU9250_H

/*
 * MPU9250 九轴 IMU 驱动：三轴加速度 + 三轴陀螺 + 三轴磁力计（AK8963）。
 * 总线：I2C（0x68/0x69）或 SPI。磁力计仅在 I2C 旁路模式下可访问，
 *       因为 AK8963 是挂在 MPU9250 内部 I2C 总线上的独立芯片。
 *
 * 参考 libdriver 风格：
 *   mpu9250_init / mpu9250_deinit / mpu9250_read / mpu9250_read_raw
 *
 * 设计要点：
 *   - init 校验 WHO_AM_I（0x75，期望 0x71）并回读校验量程配置；
 *   - I2C 模式下打开 BYPASS_EN，读取 AK8963 的 WIA(0x48) 与出厂灵敏度
 *     修正值 ASA，再进入 16 位 100Hz 连续测量模式；
 *   - read 输出 g / °/s / µT / °C 与由 X-Y 计算的航向角；
 *   - 磁力计芯片级硬铁/软铁标定未在本驱动内完成（见 mpu9250_read_mag_raw
 *     注释），调用方可基于原始值做椭球拟合后自行扣除偏置。
 *
 * 用法：
 *   mpu9250_handle_t imu;
 *   mpu9250_init(&imu, &i2c1, NULL, 0, 0x68,
 *                MPU9250_ACCEL_RANGE_4G, MPU9250_GYRO_RANGE_500DPS);
 *   mpu9250_data_t d;
 *   mpu9250_read(&imu, &d);
 */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MPU9250_ACCEL_RANGE_2G = 0,
    MPU9250_ACCEL_RANGE_4G,
    MPU9250_ACCEL_RANGE_8G,
    MPU9250_ACCEL_RANGE_16G,
    MPU9250_ACCEL_RANGE_COUNT
} mpu9250_accel_range_e;

typedef enum {
    MPU9250_GYRO_RANGE_250DPS = 0,
    MPU9250_GYRO_RANGE_500DPS,
    MPU9250_GYRO_RANGE_1000DPS,
    MPU9250_GYRO_RANGE_2000DPS,
    MPU9250_GYRO_RANGE_COUNT
} mpu9250_gyro_range_e;

typedef struct {
    float x;
    float y;
    float z;
} mpu9250_vec3_t;

typedef struct {
    mpu9250_vec3_t accel_g;         /* 加速度，g */
    mpu9250_vec3_t gyro_dps;        /* 角速度，°/s */
    mpu9250_vec3_t mag_ut;          /* 磁场，µT（磁力计缺失时全 0） */
    float temperature_c;
    float heading_deg;              /* atan2(my, mx) 转 0~360°，未做倾斜补偿 */
    bool mag_valid;                 /* 本次磁场数据是否有效 */
} mpu9250_data_t;

typedef struct {
    hal_i2c_dev_t i2c;
    hal_spi_dev_t spi;
    hal_i2c_dev_t mag;               /* AK8963 访问通道（旁路到同一 I2C 主机） */
    bool use_spi;
    bool mag_present;
    float mag_ut_per_lsb[3];        /* 按 ASA 修正后的 µT/LSB */
    mpu9250_accel_range_e accel_range;
    mpu9250_gyro_range_e gyro_range;
    float accel_lsb_per_g;
    float gyro_lsb_per_dps;
    bool initialized;
} mpu9250_handle_t;

/**
 * @brief  初始化 MPU9250（含 AK8963）
 * @param  handle       驱动句柄
 * @param  i2c          I2C 主机句柄，SPI 模式传 NULL
 * @param  spi          SPI 主机句柄，I2C 模式传 NULL
 * @param  cs           片选引脚，总线自带片选时传 0
 * @param  address      7 位从机地址，0 表示默认 0x68（SPI 模式忽略）
 * @param  accel_range  加速度量程
 * @param  gyro_range   陀螺量程
 * @return 0 成功；磁力计缺失不算失败
 */
int mpu9250_init(mpu9250_handle_t *handle, hal_i2c_t *i2c,
                 hal_spi_t *spi, hal_pin_t cs, uint16_t address,
                 mpu9250_accel_range_e accel_range, mpu9250_gyro_range_e gyro_range);

/** @brief 反初始化：陀螺/加速度进入睡眠，磁力计掉电 */
int mpu9250_deinit(mpu9250_handle_t *handle);

/** @brief 读取全部物理量（磁力计缺失时 mag_valid = false） */
int mpu9250_read(mpu9250_handle_t *handle, mpu9250_data_t *data);

/** @brief 读取加速度/陀螺原始计数（温度可为 NULL） */
int mpu9250_read_raw(mpu9250_handle_t *handle, int16_t accel[3], int16_t gyro[3],
                     int16_t *temperature_raw);

/**
 * @brief 读取磁力计原始计数（AK8963 16 位，LSB 需按 ASA 修正后换算 µT）
 * @note  原始值含硬铁偏置，驱动不做标定；SPI 模式返回 HAL_NOT_SUPPORTED
 */
int mpu9250_read_mag_raw(mpu9250_handle_t *handle, int16_t mag[3]);

/** @brief 配置输出率（4~1000 Hz） */
int mpu9250_set_sample_rate(mpu9250_handle_t *handle, uint16_t rate_hz);

/** @brief 读取中断状态寄存器（bit0 为数据就绪） */
int mpu9250_get_status(mpu9250_handle_t *handle, uint8_t *int_status);

#ifdef __cplusplus
}
#endif

#endif /* MPU9250_H */
