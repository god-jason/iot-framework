#ifndef LIS2DH12_H
#define LIS2DH12_H

/*
 * LIS2DH12 三轴加速度计驱动（I2C）。
 *
 * 参考 libdriver 风格：lis2dh12_init / lis2dh12_deinit / lis2dh12_read /
 * lis2dh12_read_raw。
 *
 * 设计要点：
 *   - init 校验 WHO_AM_I（0x33），随后配置 12 位高分辨率、BDU、量程与输出率；
 *   - read 按当前量程灵敏度把原始 LSB 换算为 g（12 位数据在 16 位中左对齐）；
 *   - read_raw 输出原始 16 位补码计数（小端）。
 *
 * 用法：
 *   lis2dh12_handle_t acc;
 *   lis2dh12_init(&acc, &i2c1, LIS2DH12_DEFAULT_ADDRESS);
 *   lis2dh12_accel_t g;
 *   lis2dh12_read(&acc, &g);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LIS2DH12_RANGE_2G = 0,      /* 1 mg/digit  */
    LIS2DH12_RANGE_4G,          /* 2 mg/digit  */
    LIS2DH12_RANGE_8G,          /* 4 mg/digit  */
    LIS2DH12_RANGE_16G,         /* 12 mg/digit */
    LIS2DH12_RANGE_COUNT
} lis2dh12_range_t;

typedef enum {
    LIS2DH12_ODR_1HZ = 0,
    LIS2DH12_ODR_10HZ,
    LIS2DH12_ODR_25HZ,
    LIS2DH12_ODR_50HZ,
    LIS2DH12_ODR_100HZ,
    LIS2DH12_ODR_200HZ,
    LIS2DH12_ODR_400HZ,
    LIS2DH12_ODR_COUNT
} lis2dh12_odr_t;

typedef struct {
    float x;
    float y;
    float z;
} lis2dh12_accel_t;

typedef struct {
    driver_i2c_t i2c;
    lis2dh12_range_t range;
    lis2dh12_odr_t odr;
    float g_per_lsb;        /* 当前量程灵敏度，g/LSB */
    bool initialized;
} lis2dh12_handle_t;

/**
 * @brief  初始化 LIS2DH12
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示使用默认 0x18
 * @return 0 成功，负值为 port_status_t（-11 表示 WHO_AM_I 不匹配）
 */
int lis2dh12_init(lis2dh12_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief 反初始化：关闭三轴，复位句柄状态（不关闭总线） */
int lis2dh12_deinit(lis2dh12_handle_t *handle);

/** @brief 读取原始计数 [x, y, z]，单位 LSB */
int lis2dh12_read_raw(lis2dh12_handle_t *handle, int16_t accel[3]);

/** @brief 读取物理量，单位 g */
int lis2dh12_read(lis2dh12_handle_t *handle, lis2dh12_accel_t *accel);

/** @brief 设置量程（重新写 CTRL_REG4，同步更新灵敏度） */
int lis2dh12_set_range(lis2dh12_handle_t *handle, lis2dh12_range_t range);

/** @brief 设置输出率（重新写 CTRL_REG1） */
int lis2dh12_set_output_rate(lis2dh12_handle_t *handle, lis2dh12_odr_t odr);

#ifdef __cplusplus
}
#endif

#endif /* LIS2DH12_H */
