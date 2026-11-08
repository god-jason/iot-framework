#ifndef AGS02MA_H
#define AGS02MA_H

/*
 * AGS02MA TVOC 传感器驱动（寄存器式 I2C，默认地址 0x1A）。
 *
 * 单位：
 *   - tvoc_ppb      : 总挥发性有机物，ppb（器件原始输出，已做 CRC 校验）
 *   - eco2_ppm_est  : 等效 CO2 估算值，ppm
 *
 * 说明：AGS02MA 只输出 TVOC；eCO2 需要按 Aosong 的换算规则或实测标定，
 * 本驱动不编造公式，仅在调用 ags02ma_set_eco2_scale() 后才输出估算值，
 * 否则 eco2_valid = false。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    uint16_t version;
    float eco2_scale;      /* ppm = scale * tvoc_ppb，0 表示不估算 */
    bool initialized;
} ags02ma_handle_t;

typedef struct {
    uint32_t tvoc_ppb;
    float eco2_ppm_est;
    bool eco2_valid;
} ags02ma_data_t;

/** @param address 7 位地址，传 0 取 0x1A */
int ags02ma_init(ags02ma_handle_t *handle, hal_i2c_t *i2c, uint16_t address);

int ags02ma_deinit(ags02ma_handle_t *handle);

/** 读版本号（init 时已读取并缓存在 handle->version） */
int ags02ma_get_version(ags02ma_handle_t *handle, uint16_t *version);

/** 设置 eCO2 线性估算系数：eco2_ppm_est = scale * tvoc_ppb */
int ags02ma_set_eco2_scale(ags02ma_handle_t *handle, float scale);

/** 读取 TVOC（每次读取至少间隔 AGS02MA_UPDATE_MS） */
int ags02ma_read(ags02ma_handle_t *handle, ags02ma_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* AGS02MA_H */
