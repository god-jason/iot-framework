#ifndef ACS712_REGISTER_H
#define ACS712_REGISTER_H

/*
 * ACS712 霍尔电流传感器 —— 模拟量器件，无寄存器地址。
 *
 * 换算关系：
 *   I(A) = (Vout(mV) - Vzero(mV)) / Sens(mV/A)
 * Vzero 为零电流输出，理论上等于供电电压的一半（5V 供电 2500mV，
 * 3.3V 供电 1650mV），实际存在几十 mV 失调，建议实测标定。
 *
 * 灵敏度按型号区分（旧实现把 66mV/A 写死，用于 5A/20A 版会得到数倍偏差）：
 *   ACS712-05B : 185 mV/A
 *   ACS712-20A : 100 mV/A
 *   ACS712-30A : 66  mV/A
 */

/* ADC 参考电压与满量程码值默认值（可用 acs712_set_reference 覆盖） */
#define ACS712_DEFAULT_VREF_MV          3300.0f
#define ACS712_DEFAULT_ADC_MAX          4095U

/* 型号灵敏度 mV/A */
#define ACS712_SENS_05B_MV_PER_A        185.0f
#define ACS712_SENS_20A_MV_PER_A        100.0f
#define ACS712_SENS_30A_MV_PER_A        66.0f

/* 默认按 30A 版（最常见的模组） */
#define ACS712_DEFAULT_SENS_MV_PER_A    ACS712_SENS_30A_MV_PER_A

/* 零电流输出：5V 供电时的典型值，需实测覆盖 */
#define ACS712_DEFAULT_ZERO_MV          2500.0f

/* 量程上限（绝对值），超出视为接线/供电异常 */
#define ACS712_CURRENT_LIMIT_A          30.0f

/* 单次 ADC 采样超时 ms */
#define ACS712_ADC_TIMEOUT_MS           10U

#endif /* ACS712_REGISTER_H */
