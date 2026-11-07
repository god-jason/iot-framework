#ifndef MAX31855_H
#define MAX31855_H

/*
 * MAX31855 K 型热电偶数字转换器驱动（SPI，只读）。
 *
 * 14 位热电偶温度（0.25 ℃）+ 12 位内部冷端温度（0.0625 ℃），
 * 提供开路 / 对电源短路 / 对地短路故障标志。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_spi_t spi;
    bool initialized;
} max31855_handle_t;

/**
 * @brief  初始化
 * @param  cs  片选引脚，PORT_PIN(0,0) 表示由外部控制
 */
int max31855_init(max31855_handle_t *handle, port_spi_handle_t *spi, port_pin_t cs);

/** @brief  反初始化 */
int max31855_deinit(max31855_handle_t *handle);

/** @brief  读取原始 32 位数据 */
int max31855_read_raw(max31855_handle_t *handle, uint32_t *raw);

/**
 * @brief  读取热电偶温度（℃）
 * @return 0 成功；存在故障位返回 PORT_HW_ERROR
 */
int max31855_read(max31855_handle_t *handle, float *celsius);

/** @brief  读取内部（冷端）温度（℃） */
int max31855_read_internal(max31855_handle_t *handle, float *celsius);

/**
 * @brief  读取故障标志（MAX31855_FAULT_OC / SCG / SCV 组合）
 * @return 0 成功，*fault 为 0 表示无故障
 */
int max31855_get_fault(max31855_handle_t *handle, uint8_t *fault);

#ifdef __cplusplus
}
#endif

#endif /* MAX31855_H */
