#ifndef YL83_REGISTER_H
#define YL83_REGISTER_H

/*
 * YL-83 雨滴检测传感器（与 FC-37 同类的雨滴板）—— 模拟输出，无寄存器。
 *
 * 换算关系（板面越湿输出越低）：
 *   percent = (Vdry - Vout) / (Vdry - Vwet) * 100
 * 输出表示雨滴板被水覆盖的相对程度，不是降雨量 mm/h。
 *
 * 默认端点为示例值，必须实测覆盖。
 */

/* ADC 参考电压与满量程码值默认值（可用 yl83_set_reference 覆盖） */
#define YL83_DEFAULT_VREF_MV        3300.0f
#define YL83_DEFAULT_ADC_MAX        4095U

/* 标定端点：板面干燥 / 完全淋湿 */
#define YL83_DEFAULT_DRY_MV         3000.0f
#define YL83_DEFAULT_WET_MV         800.0f

/* 端点差过小视为标定无效 */
#define YL83_MIN_SPAN_MV            100.0f

/* 单次 ADC 采样超时 ms */
#define YL83_ADC_TIMEOUT_MS         10U

#endif /* YL83_REGISTER_H */
