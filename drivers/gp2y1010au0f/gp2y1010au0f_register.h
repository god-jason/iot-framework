#ifndef GP2Y1010AU0F_REGISTER_H
#define GP2Y1010AU0F_REGISTER_H

/*
 * GP2Y1010AU0F 光学粉尘传感器 —— 模拟输出 + IR LED 脉冲驱动，无寄存器。
 *
 * 换算关系（旧实现保留）：
 *   0.5V 对应 0.1mg/m³ = 100µg/m³，故 1mV 对应 0.2µg/m³
 *   density_ug_m3 = (Vout(mV) - Voff(mV)) * 0.2
 * Voff 为洁净空气基线（模块典型 600mV），会随温度与老化漂移，
 * 可用 gp2y1010au0f_set_voffset 覆盖。
 *
 * LED 时序（datasheet 推荐）：
 *   LED 点亮后 280µs 采样，脉宽 320µs，周期 10ms
 */

/* ADC 参考电压与满量程码值默认值（可用 gp2y1010au0f_set_reference 覆盖） */
#define GP2Y1010AU0F_DEFAULT_VREF_MV    3300.0f
#define GP2Y1010AU0F_DEFAULT_ADC_MAX    4095U

/* 洁净空气基线电压 mV */
#define GP2Y1010AU0F_DEFAULT_VOFF_MV    600.0f

/* 电压 -> 浓度斜率 µg/m³ per mV */
#define GP2Y1010AU0F_UG_PER_MV          0.2f

/* 输出上限（模块量程约 0.5mg/m³），超出视为异常 */
#define GP2Y1010AU0F_DENSITY_MAX_UG_M3  1000.0f

/* LED 时序 µs */
#define GP2Y1010AU0F_LED_ON_DELAY_US    280U
#define GP2Y1010AU0F_LED_TAIL_DELAY_US  40U
#define GP2Y1010AU0F_SAMPLE_PERIOD_US   9680U

/* 单次 ADC 采样超时 ms */
#define GP2Y1010AU0F_ADC_TIMEOUT_MS     10U

#endif /* GP2Y1010AU0F_REGISTER_H */
