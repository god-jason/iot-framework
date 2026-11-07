#ifndef YL69_REGISTER_H
#define YL69_REGISTER_H

/*
 * YL-69 土壤湿度探头（电阻式，配 LM393 模块输出模拟量）—— 无寄存器。
 *
 * 换算关系（干燥时输出高，含水率高时输出低）：
 *   percent = (Vdry - Vout) / (Vdry - Vwet) * 100
 *
 * 默认端点按"5V 模块输出经分压后进入 3.3V ADC"的常见接法给出，
 * 土壤类型与探针镀层会显著改变端点，必须实测后用
 * yl69_set_calibration 覆盖。
 */

/* ADC 参考电压与满量程码值默认值（可用 yl69_set_reference 覆盖） */
#define YL69_DEFAULT_VREF_MV        3300.0f
#define YL69_DEFAULT_ADC_MAX        4095U

/* 标定端点：干燥空气 / 浸水 */
#define YL69_DEFAULT_DRY_MV         3000.0f
#define YL69_DEFAULT_WET_MV         1100.0f

/* 端点差过小视为标定无效 */
#define YL69_MIN_SPAN_MV            100.0f

/* 单次 ADC 采样超时 ms */
#define YL69_ADC_TIMEOUT_MS         10U

#endif /* YL69_REGISTER_H */
