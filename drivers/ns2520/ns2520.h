#ifndef NS2520_H
#define NS2520_H

/*
 * NS2520 三轴加速度计驱动（I2C）。
 *
 * 参考 libdriver 风格：ns2520_init / ns2520_deinit / ns2520_read / ns2520_read_raw。
 *
 * 设计要点：
 *   - 寄存器布局兼容 LIS2DH 系列（WHO_AM_I 0x0F、CTRL_REG1 0x20、数据 0x28）；
 *   - 该器件无公开固定 CHIP_ID，init 采用 probe + CTRL_REG1/CTRL_REG4 回读自检，
 *     写通路异常时返回 IOT_NOT_FOUND；
 *   - read 按当前量程灵敏度把原始 LSB 换算为 g；
 *   - read_raw 输出原始 16 位补码计数（小端）。
 *
 * 用法：
 *   ns2520_handle_t acc;
 *   ns2520_init(&acc, &i2c1, NS2520_DEFAULT_ADDRESS);
 *   ns2520_accel_t g;
 *   ns2520_read(&acc, &g);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NS2520_RANGE_2G = 0,        /* 1 mg/digit  */
    NS2520_RANGE_4G,            /* 2 mg/digit  */
    NS2520_RANGE_8G,            /* 4 mg/digit  */
    NS2520_RANGE_16G,           /* 12 mg/digit */
    NS2520_RANGE_COUNT
} ns2520_range_t;

typedef enum {
    NS2520_ODR_1HZ = 0,
    NS2520_ODR_10HZ,
    NS2520_ODR_25HZ,
    NS2520_ODR_50HZ,
    NS2520_ODR_100HZ,
    NS2520_ODR_200HZ,
    NS2520_ODR_400HZ,
    NS2520_ODR_COUNT
} ns2520_odr_t;

typedef struct {
    float x;
    float y;
    float z;
} ns2520_accel_t;

typedef struct {
    driver_i2c_t i2c;
    ns2520_range_t range;
    ns2520_odr_t odr;
    float g_per_lsb;        /* 当前量程灵敏度，g/LSB */
    bool initialized;
} ns2520_handle_t;

/**
 * @brief  初始化 NS2520
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示使用默认 0x18
 * @return 0 成功，负值为 iot_status_t
 */
int ns2520_init(ns2520_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/** @brief 反初始化：关闭三轴，复位句柄状态（不关闭总线） */
int ns2520_deinit(ns2520_handle_t *handle);

/** @brief 读取原始计数 [x, y, z]，单位 LSB */
int ns2520_read_raw(ns2520_handle_t *handle, int16_t accel[3]);

/** @brief 读取物理量，单位 g */
int ns2520_read(ns2520_handle_t *handle, ns2520_accel_t *accel);

/** @brief 设置量程（重新写 CTRL_REG4，同步更新灵敏度） */
int ns2520_set_range(ns2520_handle_t *handle, ns2520_range_t range);

/** @brief 设置输出率（重新写 CTRL_REG1） */
int ns2520_set_output_rate(ns2520_handle_t *handle, ns2520_odr_t odr);

#ifdef __cplusplus
}
#endif

#endif /* NS2520_H */
