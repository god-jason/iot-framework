#ifndef MS5837_REGISTER_H
#define MS5837_REGISTER_H

/*
 * MS5837-30BA 命令 / PROM 定义。
 *
 * 数据来源：TE MS5837-30BA datasheet（30 bar 深度/压力传感器）。
 *   - 命令集与 MS5611 相同：复位 / D1 / D2 / ADC 读取 / PROM 读取
 *   - PROM 8 个字，word0 高 4 位为 CRC4
 *   - 压力结果单位为 0.1 mbar（= 10 Pa）
 */

#define MS5837_DEFAULT_ADDRESS      0x76U
#define MS5837_ADDRESS_ALT          0x77U

#define MS5837_CMD_RESET            0x1EU
#define MS5837_CMD_CONV_D1(osr)     ((uint8_t)(0x40U | (((osr) & 0x07U) << 1)))
#define MS5837_CMD_CONV_D2(osr)     ((uint8_t)(0x50U | (((osr) & 0x07U) << 1)))
#define MS5837_CMD_ADC_READ         0x00U
#define MS5837_CMD_PROM_READ(i)     ((uint8_t)(0xA0U | (((i) & 0x07U) << 1)))

#define MS5837_PROM_WORDS           8U
#define MS5837_PROM_DATA_SIZE       2U
#define MS5837_ADC_DATA_SIZE        3U

/* 过采样：OSR 0..5 对应 256/512/1024/2048/4096/8192，默认 4096 */
#define MS5837_OSR_256              0x00U
#define MS5837_OSR_512              0x01U
#define MS5837_OSR_1024             0x02U
#define MS5837_OSR_2048             0x03U
#define MS5837_OSR_4096             0x04U
#define MS5837_OSR_8192             0x05U
#define MS5837_OSR_COUNT            6U
#define MS5837_OSR_DEFAULT          MS5837_OSR_4096

#define MS5837_RESET_DELAY_MS       3U

/* 深度换算常数：1 mbar = 100 Pa，ρ = 1000 kg/m^3，g = 9.80665 m/s^2 */
#define MS5837_ATM_MBAR             1013.25f
#define MS5837_WATER_DENSITY        1000.0f
#define MS5837_GRAVITY              9.80665f
/* 0.1 mbar -> m 的换算系数：0.1mbar*100/(ρg) = 10/(ρg) */
#define MS5837_MBAR_TO_METER(f)     ((f) * 100.0f / (MS5837_WATER_DENSITY * MS5837_GRAVITY))

/* 输出合理区间（30BA 量程 0..30 bar，此处按常见大气/浅水场景校验） */
#define MS5837_PRESSURE_MIN_PA      30000.0f
#define MS5837_PRESSURE_MAX_PA      3000000.0f

#endif /* MS5837_REGISTER_H */
