#ifndef CS1237_REGISTER_H
#define CS1237_REGISTER_H

/*
 * CS1237 24 位低功耗 ADC（2 线：SCLK + DRDY/DOUT，软件时序）。
 *
 * 时序要点（datasheet）：
 *   - DRDY/DOUT 变低表示新数据就绪
 *   - 每个 SCLK 上升沿输出 1 位数据，MSB 在前，共 24 位，补码格式
 *   - 正常读取必须严格 24 个时钟，多余时钟会被芯片当作配置帧起始
 */

/* 数据位数 */
#define CS1237_DATA_BITS            24U

/* 24 位补码 */
#define CS1237_DATA_SIGN_BIT        0x800000U
#define CS1237_DATA_SIGN_EXT        0xFF000000U

/* 时序（us）：datasheet 规定 SCLK 高/低电平最小 0.1us，取 1us 保证可靠 */
#define CS1237_SCLK_HIGH_US         1U
#define CS1237_SCLK_LOW_US          1U

/* 上电稳定时间（ms） */
#define CS1237_POWER_ON_MS          10U

/* 等待 DRDY 拉低的默认超时（10Hz 输出时单次转换约 100ms） */
#define CS1237_READY_TIMEOUT_MS     300U

/*
 * 配置寄存器字段。寄存器共 4 个有效位：增益 2 位 + 输出速率 2 位，
 * 具体位序与配置帧的时钟数量需按 datasheet 补充确认。
 */
#define CS1237_CFG_GAIN_SHIFT       2U
#define CS1237_CFG_RATE_SHIFT       0U

/* 增益档位（写入配置寄存器的字段值） */
#define CS1237_GAIN_FIELD_1         0U
#define CS1237_GAIN_FIELD_2         1U
#define CS1237_GAIN_FIELD_64        2U
#define CS1237_GAIN_FIELD_128       3U

/* 输出速率档位 */
#define CS1237_RATE_FIELD_10HZ      0U
#define CS1237_RATE_FIELD_40HZ      1U
#define CS1237_RATE_FIELD_640HZ     2U
#define CS1237_RATE_FIELD_1280HZ    3U

/* 默认标定参数 */
#define CS1237_DEFAULT_SCALE        1.0f
#define CS1237_DEFAULT_OFFSET       0

#endif /* CS1237_REGISTER_H */
