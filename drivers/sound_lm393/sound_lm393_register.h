#ifndef SOUND_LM393_REGISTER_H
#define SOUND_LM393_REGISTER_H

/*
 * 声音传感器模块（LM393 比较器 + 驻极体麦克风）—— 模拟输出，无寄存器。
 *
 * 模块模拟输出为麦克风偏置电压上叠加的音频交流信号（无 sound 时
 * 约在 VCC/2 附近波动）。本驱动的百分比是**相对满量程的瞬时电平**：
 *   percent = Vout / Vref * 100
 * 它不是声压级（dB SPL）：单次采样只反映波形上的一个点，真实响度
 * 需要在一段时间窗内做峰值/RMS 统计，属于上层处理。
 */

/* ADC 参考电压与满量程码值默认值（可用 sound_lm393_set_reference 覆盖） */
#define SOUND_LM393_DEFAULT_VREF_MV     3300.0f
#define SOUND_LM393_DEFAULT_ADC_MAX     4095U

/* 静音时输出偏置的典型比例（VCC/2），用于诊断提示 */
#define SOUND_LM393_QUIET_MV_RATIO      0.5f

/* 单次 ADC 采样超时 ms */
#define SOUND_LM393_ADC_TIMEOUT_MS      10U

#endif /* SOUND_LM393_REGISTER_H */
