#ifndef FLEX_REGISTER_H
#define FLEX_REGISTER_H

/*
 * 弯曲传感器（Flex/Bend Sensor）—— 电阻式分压输出，无寄存器。
 *
 * 换算关系：
 *   normalized = (Vout - Vflat) / (Vbent - Vflat) * 100     （钳位 0~100）
 * 公式对"平直端电压更高"的接法同样成立（分母为负，结果仍随弯曲单调上升）。
 *
 * 默认端点为示例值，实际取决于分压电阻与传感器型号，
 * 必须在平直与最大弯曲两个姿态下实测后用 flex_set_calibration 覆盖。
 */

/* ADC 参考电压与满量程码值默认值（可用 flex_set_reference 覆盖） */
#define FLEX_DEFAULT_VREF_MV        3300.0f
#define FLEX_DEFAULT_ADC_MAX        4095U

/* 标定端点：完全平直 / 最大弯曲 */
#define FLEX_DEFAULT_FLAT_MV        900.0f
#define FLEX_DEFAULT_BENT_MV        2400.0f

/* 端点差过小视为标定无效 */
#define FLEX_MIN_SPAN_MV            50.0f

/* 单次 ADC 采样超时 ms */
#define FLEX_ADC_TIMEOUT_MS         10U

#endif /* FLEX_REGISTER_H */
