#ifndef FSR_REGISTER_H
#define FSR_REGISTER_H

/*
 * FSR 薄膜压力电阻传感器 —— 电阻式分压输出，无寄存器。
 *
 * 典型接法（FSR 在上臂，固定电阻 R_series 在下臂）：
 *   VCC --- FSR ---+--- ADC
 *                  |
 *              R_series
 *                  |
 *                 GND
 *
 * 换算：
 *   R_fsr  = R_series * (Vref - Vout) / Vout
 *   F(g)   = K / R_fsr        （K 为经验标定常数，单位 g·Ω）
 *
 * FSR 的力-电阻关系在双对数坐标下近似直线（F ∝ R^-b），单点线性
 * 近似只在窄区间可用。默认 K 由 FSR-402 标称曲线 F ≈ 6N 对应 6kΩ
 * 推得（K ≈ 6.12e8 g·Ω），仅作量级参考，务必用已知砝码重新标定：
 *   K = F_known(g) * R_measured(Ω)
 */

/* ADC 参考电压与满量程码值默认值（可用 fsr_set_reference 覆盖） */
#define FSR_DEFAULT_VREF_MV         3300.0f
#define FSR_DEFAULT_ADC_MAX         4095U

/* 下臂固定电阻（常见 10kΩ） */
#define FSR_DEFAULT_R_SERIES_OHM    10000U

/* 力估算标定常数 g·Ω */
#define FSR_DEFAULT_FORCE_SCALE_G_OHM   6.12e8f

/* 输出上限，防止除零/极端值 */
#define FSR_PRESSURE_MAX_G          10000.0f

/* 无压力时的等效电阻（开路） */
#define FSR_OPEN_RESISTANCE         1.0e9f

/* 低于该电压视为无压力（Vout=0 时 R_fsr 为无穷大） */
#define FSR_MIN_VALID_MV            1.0f

/* 判定饱和（短路）的电压余量 mV */
#define FSR_RAIL_EPS_MV             5.0f

/* 单次 ADC 采样超时 ms */
#define FSR_ADC_TIMEOUT_MS          10U

#endif /* FSR_REGISTER_H */
