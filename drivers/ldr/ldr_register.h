#ifndef LDR_REGISTER_H
#define LDR_REGISTER_H

/*
 * LDR 光敏电阻 —— 电阻式分压输出，无寄存器。
 *
 * 接法决定极性。本驱动按"LDR 在上臂、固定电阻在下臂"建模：
 * 光照增强 -> R_ldr 减小 -> Vout 升高。
 *   percent = (Vout - Vdark) / (Vbright - Vdark) * 100   （钳位 0~100）
 * 若实际接反（光照增强电压下降），把 Vdark / Vbright 值对调即可。
 *
 * 百分比是相对亮度指标，不是照度（lux）。lux 与电阻的关系为
 * R = A * lux^-b，需逐点标定，本驱动不做。
 */

/* ADC 参考电压与满量程码值默认值（可用 ldr_set_reference 覆盖） */
#define LDR_DEFAULT_VREF_MV         3300.0f
#define LDR_DEFAULT_ADC_MAX         4095U

/* 标定端点：全暗 / 强光 */
#define LDR_DEFAULT_DARK_MV         200.0f
#define LDR_DEFAULT_BRIGHT_MV       3000.0f

/* 端点差过小视为标定无效 */
#define LDR_MIN_SPAN_MV             100.0f

/* 单次 ADC 采样超时 ms */
#define LDR_ADC_TIMEOUT_MS          10U

#endif /* LDR_REGISTER_H */
