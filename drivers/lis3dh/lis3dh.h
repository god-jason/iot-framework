#ifndef LIS3DH_H
#define LIS3DH_H

/*
 * LIS3DH 三轴加速度计驱动（I2C）。
 *
 * 参考 libdriver 风格：lis3dh_init / lis3dh_deinit / lis3dh_read / lis3dh_read_raw。
 *
 * 设计要点：
 *   - init 校验 WHO_AM_I（0x33），随后配置 12 位高分辨率（CTRL_REG4.HR）、
 *     BDU、量程与输出率；
 *   - read 按当前量程灵敏度把原始 LSB 换算为 g；
 *   - read_raw 输出原始 16 位补码计数（小端）。
 *
 * 用法：
 *   lis3dh_handle_t acc;
 *   lis3dh_init(&acc, &i2c1, LIS3DH_DEFAULT_ADDRESS);
 *   lis3dh_accel_t g;
 *   lis3dh_read(&acc, &g);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LIS3DH_RANGE_2G = 0,        /* 1 mg/digit  */
    LIS3DH_RANGE_4G,            /* 2 mg/digit  */
    LIS3DH_RANGE_8G,            /* 4 mg/digit  */
    LIS3DH_RANGE_16G,           /* 12 mg/digit */
    LIS3DH_RANGE_COUNT
} lis3dh_range_t;

typedef enum {
    LIS3DH_ODR_1HZ = 0,
    LIS3DH_ODR_10HZ,
    LIS3DH_ODR_25HZ,
    LIS3DH_ODR_50HZ,
    LIS3DH_ODR_100HZ,
    LIS3DH_ODR_200HZ,
    LIS3DH_ODR_400HZ,
    LIS3DH_ODR_COUNT
} lis3dh_odr_t;

typedef struct {
    float x;
    float y;
    float z;
} lis3dh_accel_t;

typedef struct {
    driver_i2c_t i2c;
    lis3dh_range_t range;
    lis3dh_odr_t odr;
    float g_per_lsb;        /* 当前量程灵敏度，g/LSB */
    bool initialized;
} lis3dh_handle_t;

/**
 * @brief  初始化 LIS3DH
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示使用默认 0x18
 * @return 0 成功，负值为 iot_status_t（-11 表示 WHO_AM_I 不匹配）
 */
int lis3dh_init(lis3dh_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/** @brief 反初始化：关闭三轴，复位句柄状态（不关闭总线） */
int lis3dh_deinit(lis3dh_handle_t *handle);

/** @brief 读取原始计数 [x, y, z]，单位 LSB */
int lis3dh_read_raw(lis3dh_handle_t *handle, int16_t accel[3]);

/** @brief 读取物理量，单位 g */
int lis3dh_read(lis3dh_handle_t *handle, lis3dh_accel_t *accel);

/** @brief 设置量程（重新写 CTRL_REG4，同步更新灵敏度） */
int lis3dh_set_range(lis3dh_handle_t *handle, lis3dh_range_t range);

/** @brief 设置输出率（重新写 CTRL_REG1） */
int lis3dh_set_output_rate(lis3dh_handle_t *handle, lis3dh_odr_t odr);

#ifdef __cplusplus
}
#endif

#endif /* LIS3DH_H */
