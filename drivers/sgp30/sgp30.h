#ifndef SGP30_H
#define SGP30_H

/*
 * SGP30 TVOC / eCO2 传感器驱动（命令式 I2C，默认地址 0x58）。
 *
 * 单位：
 *   - eco2_ppm : 等效二氧化碳，ppm（传感器内部算法输出，非 NDIR 实测）
 *   - tvoc_ppb : 总挥发性有机物，ppb
 *
 * 说明：eCO2/TVOC 为传感器算法输出，精度依赖 12~24h 老化；如需绝对准确，
 * 上层应按 Sensirion 建议定期保存/恢复基线（sgp30_get_baseline/set_baseline）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
} sgp30_handle_t;

typedef struct {
    uint16_t eco2_ppm;
    uint16_t tvoc_ppb;
} sgp30_data_t;

/** @param address 7 位地址，传 0 取 0x58 */
int sgp30_init(sgp30_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

int sgp30_deinit(sgp30_handle_t *handle);

/** 测量一次（内部等待 12ms 转换时间） */
int sgp30_read(sgp30_handle_t *handle, sgp30_data_t *data);

/** 读基线（建议每 1h 保存一次，掉电恢复后写回） */
int sgp30_get_baseline(sgp30_handle_t *handle, uint16_t *eco2_base, uint16_t *tvoc_base);

/** 写回基线 */
int sgp30_set_baseline(sgp30_handle_t *handle, uint16_t eco2_base, uint16_t tvoc_base);

/**
 * @brief  设置湿度补偿
 * @param  humidity_mg_m3 绝对湿度，单位 mg/m3 × 1000（如 12.5 g/m3 -> 12500）
 */
int sgp30_set_humidity(sgp30_handle_t *handle, uint32_t humidity_mg_m3);

#ifdef __cplusplus
}
#endif

#endif /* SGP30_H */
