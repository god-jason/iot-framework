#ifndef NTC_REGISTER_H
#define NTC_REGISTER_H

/*
 * NTC 热敏电阻参数与公式常量。
 *
 * 典型接法（NTC 在下臂）：
 *   VCC --- R_series ---+--- ADC
 *                       |
 *                      NTC
 *                       |
 *                      GND
 * 则 R_ntc = R_series * V / (VCC - V)，V = raw / adc_max * vref。
 *
 * B 值公式：
 *   1/T = 1/T25 + ln(R/R25) / B      （T 为开氏温度）
 */

/* 默认器件参数（10kΩ @25℃, B=3950 为常见型号） */
#define NTC_DEFAULT_R_SERIES        10000U
#define NTC_DEFAULT_R25             10000.0f
#define NTC_DEFAULT_BETA            3950.0f
#define NTC_DEFAULT_VREF_MV         3300.0f
#define NTC_DEFAULT_ADC_MAX         4095U

/* 开氏温度换算 */
#define NTC_KELVIN_OFFSET           273.15f
#define NTC_T25_K                   298.15f

/* 输出有效范围（超出视为传感器异常） */
#define NTC_TEMP_MIN_C              (-40.0f)
#define NTC_TEMP_MAX_C              150.0f

/* 判定饱和（开路）的电压余量，单位 mV */
#define NTC_VREF_EPS_MV             1.0f

/* 开路时 NTC 视为极大电阻，避免 log(0)/除零 */
#define NTC_OPEN_RESISTANCE         1.0e9f

#endif /* NTC_REGISTER_H */
