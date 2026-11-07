#ifndef BMI270_H
#define BMI270_H

/*
 * BMI270 六轴 IMU 驱动（三轴加速度 + 三轴陀螺），I2C 接口。
 *
 * 参考 libdriver 风格：bmi270_init / bmi270_deinit / bmi270_read / bmi270_read_raw。
 *
 * 设计要点：
 *   - init 先 probe 从机，再校验 CHIP_ID（0x24），然后配置量程与输出率，
 *     最后使能加速度计与陀螺仪并延时等待启动；
 *   - read 按当前量程灵敏度把原始 LSB 换算为 g 与 °/s；
 *   - read_raw 输出原始 16 位补码计数（小端）；
 *   - 量程与输出率可在 init 后通过 set_xxx 重新配置。
 *
 * 注意：BMI270 完整工作需要在初始化阶段加载 ~8KB 配置固件。本驱动不携带固件
 * 镜像，只完成寄存器配置与数据读取；若器件未加载固件，INTERNAL_STATUS.msg
 * 不会为 0x01，此时数据寄存器可能保持 0。可通过 bmi270_get_internal_status()
 * 查询该状态，必要时由上层完成固件加载。
 *
 * 用法：
 *   bmi270_handle_t imu;
 *   bmi270_init(&imu, &i2c1, BMI270_DEFAULT_ADDRESS);
 *   bmi270_set_accel_range(&imu, BMI270_ACCEL_RANGE_8G);
 *   bmi270_data_t d;
 *   bmi270_read(&imu, &d);   // d.accel_g / d.gyro_dps
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BMI270_ACCEL_RANGE_2G = 0,      /* 16384 LSB/g */
    BMI270_ACCEL_RANGE_4G,          /*  8192 LSB/g */
    BMI270_ACCEL_RANGE_8G,          /*  4096 LSB/g */
    BMI270_ACCEL_RANGE_16G,         /*  2048 LSB/g */
    BMI270_ACCEL_RANGE_COUNT
} bmi270_accel_range_t;

typedef enum {
    BMI270_GYRO_RANGE_2000DPS = 0,  /* 16.384 LSB/(°/s) */
    BMI270_GYRO_RANGE_1000DPS,      /* 32.768 LSB/(°/s) */
    BMI270_GYRO_RANGE_500DPS,       /* 65.536 LSB/(°/s) */
    BMI270_GYRO_RANGE_250DPS,       /* 131.072 LSB/(°/s) */
    BMI270_GYRO_RANGE_125DPS,       /* 262.144 LSB/(°/s) */
    BMI270_GYRO_RANGE_COUNT
} bmi270_gyro_range_t;

typedef enum {
    BMI270_ODR_25HZ = 0,
    BMI270_ODR_50HZ,
    BMI270_ODR_100HZ,
    BMI270_ODR_200HZ,
    BMI270_ODR_400HZ,
    BMI270_ODR_800HZ,
    BMI270_ODR_1600HZ,
    BMI270_ODR_COUNT
} bmi270_odr_t;

typedef struct {
    float x;
    float y;
    float z;
} bmi270_vec3_t;

typedef struct {
    bmi270_vec3_t accel_g;      /* 加速度，单位 g */
    bmi270_vec3_t gyro_dps;     /* 角速度，单位 °/s */
} bmi270_data_t;

typedef struct {
    driver_i2c_t i2c;
    bmi270_accel_range_t accel_range;
    bmi270_gyro_range_t gyro_range;
    bmi270_odr_t odr;
    float accel_lsb_per_g;      /* 当前量程灵敏度 */
    float gyro_lsb_per_dps;
    bool initialized;
} bmi270_handle_t;

/**
 * @brief  初始化 BMI270
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示使用默认 0x68
 * @return 0 成功，负值为 port_status_t（-11 表示 probe / CHIP_ID 不匹配）
 */
int bmi270_init(bmi270_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief 反初始化：关闭加速度计与陀螺仪，复位句柄状态（不关闭总线） */
int bmi270_deinit(bmi270_handle_t *handle);

/**
 * @brief  读取原始计数
 * @param  accel  加速度原始值 [ax, ay, az]，单位 LSB
 * @param  gyro   角速度原始值 [gx, gy, gz]，单位 LSB
 */
int bmi270_read_raw(bmi270_handle_t *handle, int16_t accel[3], int16_t gyro[3]);

/** @brief 读取物理量：加速度 g，角速度 °/s */
int bmi270_read(bmi270_handle_t *handle, bmi270_data_t *data);

/** @brief 设置加速度量程（重新写 ACC_RANGE，同步更新灵敏度） */
int bmi270_set_accel_range(bmi270_handle_t *handle, bmi270_accel_range_t range);

/** @brief 设置陀螺量程（重新写 GYR_RANGE，同步更新灵敏度） */
int bmi270_set_gyro_range(bmi270_handle_t *handle, bmi270_gyro_range_t range);

/** @brief 设置输出率（同时配置加速度计与陀螺仪） */
int bmi270_set_output_rate(bmi270_handle_t *handle, bmi270_odr_t odr);

/**
 * @brief  读取内部状态寄存器
 * @param  msg  内部状态 msg 字段，0x01 表示配置固件已加载、进入应用模式
 */
int bmi270_get_internal_status(bmi270_handle_t *handle, uint8_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* BMI270_H */
