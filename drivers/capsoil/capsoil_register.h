#ifndef CAPSOIL_REGISTER_H
#define CAPSOIL_REGISTER_H

/*
 * 电容式土壤湿度传感器 v1.2 —— 模拟量器件，无寄存器地址。
 *
 * 换算关系（输出随含水率升高而**下降**，与电阻式相反）：
 *   percent = (Vdry - Vout) / (Vdry - Vwet) * 100
 *
 * 默认值仅作示例：3.3V 供电、探头插在空气中的典型读数约 2600mV，
 * 插入水中约 1100mV。土壤类型、供电、探头涂层都会显著改变端点，
 * 必须在目标介质中实测后用 capsoil_set_calibration 覆盖。
 */

/* ADC 参考电压与满量程码值默认值（可用 capsoil_set_reference 覆盖） */
#define CAPSOIL_DEFAULT_VREF_MV     3300.0f
#define CAPSOIL_DEFAULT_ADC_MAX     4095U

/* 标定端点：干燥空气 / 水中 */
#define CAPSOIL_DEFAULT_DRY_MV      2600.0f
#define CAPSOIL_DEFAULT_WET_MV      1100.0f

/* 端点差过小视为标定无效 */
#define CAPSOIL_MIN_SPAN_MV         100.0f

/* 判定贴顶（开路/未接探头）的电压余量 mV */
#define CAPSOIL_RAIL_EPS_MV         20.0f

/* 单次 ADC 采样超时 ms */
#define CAPSOIL_ADC_TIMEOUT_MS      10U

#endif /* CAPSOIL_REGISTER_H */
