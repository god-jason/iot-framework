#ifndef MPU6050_H
#define MPU6050_H

/*
 * MPU6050 六轴 IMU 驱动（三轴加速度 + 三轴陀螺 + 片上温度），I2C 接口。
 *
 * 参考 libdriver 风格：
 *   mpu6050_init / mpu6050_deinit / mpu6050_read / mpu6050_read_raw
 *
 * 设计要点：
 *   - init 校验 WHO_AM_I(0x75，期望 0x68)，软复位后回读校验量程寄存器，
 *     作为通信与写通路自检；
 *   - read 按当前量程灵敏度把原始 LSB 换算为 g、°/s 与 °C；
 *   - read_raw 输出原始 16 位补码计数（大端）；
 *   - 量程、输出率与 DLPF 带宽均可配置。
 *
 * 用法：
 *   mpu6050_handle_t imu;
 *   mpu6050_init(&imu, &i2c1, MPU6050_DEFAULT_ADDRESS,
 *                MPU6050_ACCEL_RANGE_4G, MPU6050_GYRO_RANGE_500DPS);
 *   mpu6050_data_t d;
 *   mpu6050_read(&imu, &d);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MPU6050_ACCEL_RANGE_2G = 0,     /* 16384 LSB/g */
    MPU6050_ACCEL_RANGE_4G,         /*  8192 LSB/g */
    MPU6050_ACCEL_RANGE_8G,         /*  4096 LSB/g */
    MPU6050_ACCEL_RANGE_16G,        /*  2048 LSB/g */
    MPU6050_ACCEL_RANGE_COUNT
} mpu6050_accel_range_t;

typedef enum {
    MPU6050_GYRO_RANGE_250DPS = 0,  /* 131.0 LSB/(°/s) */
    MPU6050_GYRO_RANGE_500DPS,      /*  65.5 LSB/(°/s) */
    MPU6050_GYRO_RANGE_1000DPS,     /*  32.8 LSB/(°/s) */
    MPU6050_GYRO_RANGE_2000DPS,     /*  16.4 LSB/(°/s) */
    MPU6050_GYRO_RANGE_COUNT
} mpu6050_gyro_range_t;

typedef enum {
    MPU6050_DLPF_BW_260HZ = 0,
    MPU6050_DLPF_BW_184HZ,
    MPU6050_DLPF_BW_94HZ,
    MPU6050_DLPF_BW_44HZ,
    MPU6050_DLPF_BW_21HZ,
    MPU6050_DLPF_BW_10HZ,
    MPU6050_DLPF_BW_5HZ
} mpu6050_dlpf_t;

typedef struct {
    float x;
    float y;
    float z;
} mpu6050_vec3_t;

typedef struct {
    mpu6050_vec3_t accel_g;         /* 加速度，单位 g */
    mpu6050_vec3_t gyro_dps;        /* 角速度，单位 °/s */
    float temperature_c;            /* 片上温度，单位 °C */
} mpu6050_data_t;

typedef struct {
    driver_i2c_t i2c;
    mpu6050_accel_range_t accel_range;
    mpu6050_gyro_range_t gyro_range;
    mpu6050_dlpf_t dlpf;
    float accel_lsb_per_g;          /* 当前量程灵敏度 */
    float gyro_lsb_per_dps;
    bool initialized;
} mpu6050_handle_t;

/**
 * @brief  初始化 MPU6050
 * @param  handle       驱动句柄
 * @param  i2c          已初始化的 I2C 主机句柄
 * @param  address      7 位从机地址，0 表示使用默认 0x68
 * @param  accel_range  加速度量程
 * @param  gyro_range   陀螺量程
 * @return 0 成功，负值为 iot_status_t
 */
int mpu6050_init(mpu6050_handle_t *handle, iot_i2c_t *i2c, uint16_t address,
                 mpu6050_accel_range_t accel_range, mpu6050_gyro_range_t gyro_range);

/** @brief 反初始化：进入睡眠并复位句柄状态（不关闭总线） */
int mpu6050_deinit(mpu6050_handle_t *handle);

/** @brief 读取物理量：g / °/s / °C */
int mpu6050_read(mpu6050_handle_t *handle, mpu6050_data_t *data);

/**
 * @brief 读取原始计数
 * @param  accel          加速度原始值 [ax, ay, az]
 * @param  gyro           角速度原始值 [gx, gy, gz]
 * @param  temperature_raw 温度原始值，可为 NULL
 */
int mpu6050_read_raw(mpu6050_handle_t *handle, int16_t accel[3], int16_t gyro[3],
                     int16_t *temperature_raw);

/** @brief 配置输出率（4~1000 Hz，内部换算 SMPLRT_DIV） */
int mpu6050_set_sample_rate(mpu6050_handle_t *handle, uint16_t rate_hz);

/** @brief 配置数字低通带宽（同时决定陀螺仪内部采样率） */
int mpu6050_set_dlpf(mpu6050_handle_t *handle, mpu6050_dlpf_t dlpf);

/** @brief 读取中断状态寄存器（bit0 为数据就绪） */
int mpu6050_get_status(mpu6050_handle_t *handle, uint8_t *int_status);

#ifdef __cplusplus
}
#endif

#endif /* MPU6050_H */
