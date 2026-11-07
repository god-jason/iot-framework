#ifndef MLX90640_H
#define MLX90640_H

/*
 * MLX90640 32x24 红外热像仪驱动（Melexis，I2C 7 位地址 0x33）。
 *
 * 参考 libdriver 风格：
 *   mlx90640_init / mlx90640_deinit / mlx90640_read / mlx90640_read_raw
 * 所有硬件访问只经由 drivers/common/driver_io.h。
 *
 * 本驱动负责：16 位寄存器访问、RAM 分块读取、Chess 模式子页跟踪、
 * 转换周期等待与长度校验。
 *
 * 已知限制（重要）：
 *   精确测温需要读取并解析 EEPROM(0x2400 起 832 字) 的全部校准系数，
 *   对每个像素做 offset/灵敏度/Ta 补偿。本驱动在行数约束内不实现该校准，
 *   read() 输出的是"原始 ADC 码 × 0.25 K/LSB - 273.15"的线性近似值，
 *   适合热场成像与相对比较；需要绝对精度时请使用完整校准实现，
 *   或直接使用 read_raw() 拿原始码自行换算。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    uint32_t frame_period_ms;   /* 由刷新率换算的帧周期 */
    uint8_t subpage;            /* 最近一帧所属子页（Chess 模式 0/1） */
    bool initialized;
} mlx90640_handle_t;

/**
 * @brief  初始化 MLX90640（probe + 设置 8Hz 刷新率并回读校验 + 预热）
 * @param  handle   驱动句柄
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位从机地址，0 表示默认 0x33
 * @return 0 成功，负值为 port_status_t
 */
int mlx90640_init(mlx90640_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

/** @brief 反初始化（不向器件下发停止命令，避免误改控制寄存器） */
int mlx90640_deinit(mlx90640_handle_t *handle);

/**
 * @brief  读取一帧温度矩阵
 * @param  temperatures  调用方缓冲区，长度 >= len
 * @param  len           像素个数，1~768（32x24 行优先），单位 ℃
 * @return 0 成功；len 越界或缓冲区为 NULL 返回 PORT_INVALID_PARAM
 */
int mlx90640_read(mlx90640_handle_t *handle, float *temperatures, size_t len);

/**
 * @brief  读取一帧原始 ADC 码
 * @param  raw  调用方缓冲区，长度 >= len
 * @param  len  像素个数，1~768，元素为 16 位 RAM 字（低 2 位为标志位）
 */
int mlx90640_read_raw(mlx90640_handle_t *handle, int16_t *raw, size_t len);

/** @brief 读取 STATUS(0x800F) 原始值（含子页号与错误位） */
int mlx90640_get_status(mlx90640_handle_t *handle, uint16_t *status);

#ifdef __cplusplus
}
#endif

#endif /* MLX90640_H */
