#ifndef TMP36_REGISTER_H
#define TMP36_REGISTER_H

/*
 * TMP36 模拟温度传感器 —— 模拟电压输出，无寄存器。
 *
 * 换算关系：
 *   T(℃) = (Vout(mV) - 500) / 10      （10mV/℃ + 500mV 零点偏移）
 * 与 LM35 的区别正是这 500mV 偏移，使 TMP36 可以测负温度
 * （-40℃ 对应 100mV）。
 */

/* ADC 参考电压与满量程码值默认值（可用 tmp36_set_reference 覆盖） */
#define TMP36_DEFAULT_VREF_MV       3300.0f
#define TMP36_DEFAULT_ADC_MAX       4095U

/* 0℃ 时的输出电压 mV */
#define TMP36_ZERO_OFFSET_MV        500.0f

/* 灵敏度 mV/℃ */
#define TMP36_MV_PER_C              10.0f

/* 数据手册范围 */
#define TMP36_TEMP_MIN_C            (-40.0f)
#define TMP36_TEMP_MAX_C            125.0f

/* 单次 ADC 采样超时 ms */
#define TMP36_ADC_TIMEOUT_MS        10U

#endif /* TMP36_REGISTER_H */
