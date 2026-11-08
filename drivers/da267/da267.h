#ifndef DA267_H
#define DA267_H

/*
 * DA267 三轴加速度计驱动（I2C）。
 *
 * 参考 libdriver 风格：da267_init / da267_deinit / da267_read / da267_read_raw。
 *
 * 设计要点：
 *   - 寄存器布局兼容 LIS2DH 系列（WHO_AM_I 0x0F、CTRL_REG1 0x20、数据 0x28）；
 *   - 该器件无公开固定 CHIP_ID，init 采用 probe + CTRL_REG1/CTRL_REG4 回读自检，
 *     写通路异常时返回 HAL_NOT_FOUND；
 *   - read 按当前量程灵敏度把原始 LSB 换算为 g（12 位数据在 16 位中左对齐）；
 *   - read_raw 输出原始 16 位补码计数（小端）。
 *
 * 用法：
 *   da267_handle_t acc;
 *   da267_init(&acc, &i2c1, DA267_DEFAULT_ADDRESS);
 *   da267_accel_t g;
 *   da267_read(&acc, &g);
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DA267_RANGE_2G = 0,     /* 1 mg/digit  */
    DA267_RANGE_4G,         /* 2 mg/digit  */
    DA267_RANGE_8G,         /* 4 mg/digit  */
    DA267_RANGE_16G,        /* 12 mg/digit */
    DA267_RANGE_COUNT
} da267_range_t;

typedef enum {
    DA267_ODR_1HZ = 0,
    DA267_ODR_10HZ,
    DA267_ODR_25HZ,
    DA267_ODR_50HZ,
    DA267_ODR_100HZ,
    DA267_ODR_200HZ,
    DA267_ODR_400HZ,
    DA267_ODR_COUNT
} da267_odr_t;

typedef struct {
    float x;
    float y;
    float z;
} da267_accel_t;

typedef struct {
    driver_i2c_t i2c;
    da267_range_t range;
    da267_odr_t odr;
    float g_per_lsb;        /* 当前量程灵敏度，g/LSB */
    bool initialized;
} da267_handle_t;

/**
 * @brief  初始化 DA267
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示使用默认 0x18
 * @return 0 成功，负值为 hal_status_t
 */
int da267_init(da267_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

/** @brief 反初始化：器件进入掉电（各轴关闭），复位句柄状态（不关闭总线） */
int da267_deinit(da267_handle_t *handle);

/** @brief 读取原始计数 [x, y, z]，单位 LSB */
int da267_read_raw(da267_handle_t *handle, int16_t accel[3]);

/** @brief 读取物理量，单位 g */
int da267_read(da267_handle_t *handle, da267_accel_t *accel);

/** @brief 设置量程（重新写 CTRL_REG4，同步更新灵敏度） */
int da267_set_range(da267_handle_t *handle, da267_range_t range);

/** @brief 设置输出率（重新写 CTRL_REG1） */
int da267_set_output_rate(da267_handle_t *handle, da267_odr_t odr);

#ifdef __cplusplus
}
#endif

#endif /* DA267_H */
