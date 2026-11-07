#ifndef FC28_REGISTER_H
#define FC28_REGISTER_H

/*
 * FC-28 土壤湿度传感器（电阻式，LM393 比较器模块的模拟输出）—— 无寄存器。
 *
 * 换算关系（干燥时输出高，含水率高时输出低）：
 *   percent = (Vdry - Vout) / (Vdry - Vwet) * 100
 *
 * 默认端点按"5V 模块输出经 1:1 分压后送入 3.3V ADC"的常见接法给出；
 * 若模块直接用 5V 供电且未分压，输出会超过 ADC 量程而饱和。
 * 端点必须按实际土壤实测标定（用 fc28_set_calibration 覆盖）。
 */

/* ADC 参考电压与满量程码值默认值（可用 fc28_set_reference 覆盖） */
#define FC28_DEFAULT_VREF_MV        3300.0f
#define FC28_DEFAULT_ADC_MAX        4095U

/* 标定端点：干燥空气 / 浸水 */
#define FC28_DEFAULT_DRY_MV         3000.0f
#define FC28_DEFAULT_WET_MV         1200.0f

/* 端点差过小视为标定无效 */
#define FC28_MIN_SPAN_MV            100.0f

/* 单次 ADC 采样超时 ms */
#define FC28_ADC_TIMEOUT_MS         10U

#endif /* FC28_REGISTER_H */
