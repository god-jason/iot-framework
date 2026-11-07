#ifndef HX711_REGISTER_H
#define HX711_REGISTER_H

/*
 * HX711 24 位称重 ADC 时序常量。
 *
 * HX711 无寄存器接口，只有两根线：
 *   - PD_SCK：主机输出时钟
 *   - DOUT  ：芯片数据输出（低电平表示转换完成）
 * 每次转换读出 24 位数据，其后附加 1~3 个时钟脉冲选择下一次的通道/增益。
 */

/* 数据位数 */
#define HX711_DATA_BITS             24U

/* 第 25~27 个脉冲与下一次通道/增益的对应关系 */
#define HX711_PULSES_A_GAIN_128     25U
#define HX711_PULSES_B_GAIN_32      26U
#define HX711_PULSES_A_GAIN_64      27U

/* 24 位补码的最高位掩码 */
#define HX711_DATA_SIGN_BIT         0x800000U
#define HX711_DATA_SIGN_EXT         0xFF000000U

/*
 * 时序（us）。datasheet 规定 PD_SCK 高/低电平各 0.2~50us；
 * 这里取 1us 以兼顾速度与可靠采样。
 */
#define HX711_SCK_HIGH_US           1U
#define HX711_SCK_LOW_US            1U

/* 上电后内部稳压与震荡器稳定时间，datasheet 未给出明确值，取保守值 */
#define HX711_POWER_ON_MS           400U

/* 等待 DOUT 拉低（转换完成）的默认超时。10Hz 输出时单次转换约 100ms */
#define HX711_READY_TIMEOUT_MS      300U

/* 默认标定参数 */
#define HX711_DEFAULT_SCALE         1.0f
#define HX711_DEFAULT_OFFSET        0

#endif /* HX711_REGISTER_H */
