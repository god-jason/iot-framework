#ifndef MAX6675_H
#define MAX6675_H

/*
 * MAX6675 K 型热电偶数字转换器驱动（SPI，只读）。
 *
 * 冷端补偿在片内完成，分辨率 0.25 ℃，量程 0 - 1024 ℃。
 * 参考 libdriver 风格：max6675_init / max6675_read。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_spi_t spi;
    bool initialized;
} max6675_handle_t;

/**
 * @brief  初始化（器件无配置寄存器，仅保存总线并拉高片选）
 * @param  cs  片选引脚，HAL_PIN(0,0) 表示由外部控制
 */
int max6675_init(max6675_handle_t *handle, hal_spi_t *spi, hal_pin_t cs);

/** @brief  反初始化 */
int max6675_deinit(max6675_handle_t *handle);

/** @brief  读取原始 16 位数据 */
int max6675_read_raw(max6675_handle_t *handle, uint16_t *raw);

/**
 * @brief  读取温度（℃）
 * @return 0 成功；热电偶开路返回 HAL_HW_ERROR
 */
int max6675_read(max6675_handle_t *handle, float *celsius);

#ifdef __cplusplus
}
#endif

#endif /* MAX6675_H */
