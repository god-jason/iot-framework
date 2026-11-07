#ifndef LM35_REGISTER_H
#define LM35_REGISTER_H

/*
 * LM35 模拟温度传感器 —— 模拟电压输出，无寄存器。
 *
 * 换算关系：
 *   T(℃) = Vout(mV) / 10        （10mV/℃ 线性，0℃ 对应 0mV）
 * LM35 无需偏移修正，但**无法测量负温度**（输出不能为负），
 * 需要负温测量时选 LM35DZ 以外的 LM35C/LM35CA 并加负电源偏置。
 *
 * 精度注意：1℃ 对应 10mV，12 位 ADC 在 3.3V 基准下 LSB 约 0.8mV
 * （0.08℃），量化误差可接受；基准误差会直接变成温度误差。
 */

/* ADC 参考电压与满量程码值默认值（可用 lm35_set_reference 覆盖） */
#define LM35_DEFAULT_VREF_MV        3300.0f
#define LM35_DEFAULT_ADC_MAX        4095U

/* 灵敏度 mV/℃ */
#define LM35_MV_PER_C               10.0f

/* 数据手册范围（超出即判为异常读数） */
#define LM35_TEMP_MIN_C             0.0f
#define LM35_TEMP_MAX_C             150.0f

/* 单次 ADC 采样超时 ms */
#define LM35_ADC_TIMEOUT_MS         10U

#endif /* LM35_REGISTER_H */
