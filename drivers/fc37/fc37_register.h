#ifndef FC37_REGISTER_H
#define FC37_REGISTER_H

/*
 * FC-37 雨滴/雨量检测传感器（电阻式雨滴板，模拟输出）—— 无寄存器。
 *
 * 换算关系（板面越湿输出越低）：
 *   percent = (Vdry - Vout) / (Vdry - Vwet) * 100
 * 这里的百分比表示"雨滴板被水覆盖的相对程度"，不是降雨量 mm，
 * 换算成 mm/h 需要额外标定，本驱动不做。
 *
 * 默认端点为 3.3V 供电、经分压后的常见读数，必须实测覆盖。
 */

/* ADC 参考电压与满量程码值默认值（可用 fc37_set_reference 覆盖） */
#define FC37_DEFAULT_VREF_MV        3300.0f
#define FC37_DEFAULT_ADC_MAX        4095U

/* 标定端点：板面干燥 / 完全淋湿 */
#define FC37_DEFAULT_DRY_MV         3000.0f
#define FC37_DEFAULT_WET_MV         800.0f

/* 端点差过小视为标定无效 */
#define FC37_MIN_SPAN_MV            100.0f

/* 单次 ADC 采样超时 ms */
#define FC37_ADC_TIMEOUT_MS         10U

#endif /* FC37_REGISTER_H */
