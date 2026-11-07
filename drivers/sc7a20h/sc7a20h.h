#ifndef SC7A20H_H
#define SC7A20H_H

/*
 * SC7A20H 三轴加速度计驱动（I2C）。
 *
 * 参考 libdriver 风格：sc7a20h_init / sc7a20h_deinit / sc7a20h_read /
 * sc7a20h_read_raw。
 *
 * 设计要点：
 *   - init 校验 WHO_AM_I（0x11），随后配置量程、BDU、输出率与三轴使能；
 *   - read 按当前量程灵敏度把原始 LSB 换算为 g；
 *   - read_raw 输出原始 16 位补码计数（小端）。
 *
 * 用法：
 *   sc7a20h_handle_t acc;
 *   sc7a20h_init(&acc, &i2c1, SC7A20H_DEFAULT_ADDRESS);
 *   sc7a20h_accel_t g;
 *   sc7a20h_read(&acc, &g);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SC7A20H_RANGE_2G = 0,       /* 1 mg/digit  */
    SC7A20H_RANGE_4G,           /* 2 mg/digit  */
    SC7A20H_RANGE_8G,           /* 4 mg/digit  */
    SC7A20H_RANGE_16G,          /* 12 mg/digit */
    SC7A20H_RANGE_COUNT
} sc7a20h_range_t;

typedef enum {
    SC7A20H_ODR_1HZ = 0,
    SC7A20H_ODR_10HZ,
    SC7A20H_ODR_25HZ,
    SC7A20H_ODR_50HZ,
    SC7A20H_ODR_100HZ,
    SC7A20H_ODR_200HZ,
    SC7A20H_ODR_400HZ,
    SC7A20H_ODR_COUNT
} sc7a20h_odr_t;

typedef struct {
    float x;
    float y;
    float z;
} sc7a20h_accel_t;

typedef struct {
    driver_i2c_t i2c;
    sc7a20h_range_t range;
    sc7a20h_odr_t odr;
    float g_per_lsb;        /* 当前量程灵敏度，g/LSB */
    bool initialized;
} sc7a20h_handle_t;

/**
 * @brief  初始化 SC7A20H
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示使用默认 0x18
 * @return 0 成功，负值为 iot_status_t（-11 表示 WHO_AM_I 不匹配）
 */
int sc7a20h_init(sc7a20h_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

/** @brief 反初始化：关闭三轴，复位句柄状态（不关闭总线） */
int sc7a20h_deinit(sc7a20h_handle_t *handle);

/** @brief 读取原始计数 [x, y, z]，单位 LSB */
int sc7a20h_read_raw(sc7a20h_handle_t *handle, int16_t accel[3]);

/** @brief 读取物理量，单位 g */
int sc7a20h_read(sc7a20h_handle_t *handle, sc7a20h_accel_t *accel);

/** @brief 设置量程（重新写 CTRL_REG4，同步更新灵敏度） */
int sc7a20h_set_range(sc7a20h_handle_t *handle, sc7a20h_range_t range);

/** @brief 设置输出率（重新写 CTRL_REG1） */
int sc7a20h_set_output_rate(sc7a20h_handle_t *handle, sc7a20h_odr_t odr);

#ifdef __cplusplus
}
#endif

#endif /* SC7A20H_H */
