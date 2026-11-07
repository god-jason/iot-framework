#ifndef MLX90614_H
#define MLX90614_H

/*
 * Melexis MLX90614 非接触红外测温驱动（SMBus 协议，兼容 I2C）。
 *
 * 参考 libdriver 风格：
 *   mlx90614_init / mlx90614_deinit / mlx90614_read / mlx90614_write
 * 温度换算：T(℃) = raw * 0.02 - 273.15
 * 读操作使用 SMBus Read Word（重复起始），并校验 PEC（CRC-8, poly 0x07）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float ambient_c;    /* 环境温度 */
    float object_c;     /* 目标温度 */
} mlx90614_data_t;

typedef struct {
    driver_i2c_t i2c;
    float emissivity;   /* 缓存的上次写入/读取的发射率 */
    bool initialized;
} mlx90614_handle_t;

/**
 * @brief  初始化 MLX90614
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址，0 表示默认 0x5A
 */
int mlx90614_init(mlx90614_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

int mlx90614_deinit(mlx90614_handle_t *handle);

/** 读取环境温度与目标温度 */
int mlx90614_read(mlx90614_handle_t *handle, mlx90614_data_t *data);

/** 读取单个温度命令的原始值（cmd 取 MLX90614_CMD_*，含 PEC 校验） */
int mlx90614_read_raw(mlx90614_handle_t *handle, uint8_t cmd, uint16_t *raw);

/** 读取 RAM / EEPROM 字（16 位，含 PEC 校验） */
int mlx90614_read_word(mlx90614_handle_t *handle, uint8_t cmd, uint16_t *value);

/** 写入 EEPROM 字（含擦除 + 写两阶段，需 datasheet 时序） */
int mlx90614_write_eeprom(mlx90614_handle_t *handle, uint8_t cmd, uint16_t value);

/** 设置发射率（0.1 ~ 1.0），写入 EEPROM 0x24 */
int mlx90614_set_emissivity(mlx90614_handle_t *handle, float emissivity);

/** 读取并换算发射率 */
int mlx90614_get_emissivity(mlx90614_handle_t *handle, float *emissivity);

/** 读取标志寄存器（错误标志） */
int mlx90614_read_flags(mlx90614_handle_t *handle, uint16_t *flags);

#ifdef __cplusplus
}
#endif

#endif /* MLX90614_H */
