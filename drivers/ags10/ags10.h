#ifndef AGS10_H
#define AGS10_H

/*
 * AGS10 TVOC 传感器驱动（寄存器式 I2C，默认地址 0x1A）。
 *
 * 单位：
 *   - raw            : 器件原始 32 位读数（AGS10 输出的是敏感电阻值）
 *   - resistance_ohm : 敏感电阻 Ω（即 raw，按 datasheet 解读）
 *   - tvoc_ppm_est   : TVOC 估算值，ppm
 *
 * 说明：AGS10 输出的是电阻值，TVOC 浓度需按 datasheet 给出的
 * Rs/R0 与浓度关系曲线（幂函数）换算，本驱动不编造系数；只有调用
 * ags10_set_tvoc_scale() 后才输出线性近似估算值，否则 tvoc_valid = false。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    uint16_t version;
    float tvoc_scale;
    bool initialized;
} ags10_handle_t;

typedef struct {
    uint32_t raw;
    float resistance_ohm;
    float tvoc_ppm_est;
    bool tvoc_valid;
} ags10_data_t;

/** @param address 7 位地址，传 0 取 0x1A */
int ags10_init(ags10_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

int ags10_deinit(ags10_handle_t *handle);

/** 读版本号（init 时已读取并缓存在 handle->version） */
int ags10_get_version(ags10_handle_t *handle, uint16_t *version);

/** 设置 TVOC 线性近似系数：tvoc_ppm_est = scale * raw */
int ags10_set_tvoc_scale(ags10_handle_t *handle, float scale);

/** 读取电阻原始值（间隔建议 ≥ AGS10_UPDATE_MS） */
int ags10_read(ags10_handle_t *handle, ags10_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* AGS10_H */
