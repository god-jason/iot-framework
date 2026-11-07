#ifndef MS5611_REGISTER_H
#define MS5611_REGISTER_H

/*
 * MS5611-01BA 命令 / PROM 定义。
 *
 * 数据来源：TE MS5611-01BA03 datasheet。
 *   - 无寄存器地址概念，全部通过命令字节操作
 *   - PROM 8 个字（每字 16 位），word0 高 4 位为 CRC4
 *   - D1 = 压力 ADC，D2 = 温度 ADC，均为 24 位
 */

#define MS5611_DEFAULT_ADDRESS      0x77U   /* CSB 拉高 */
#define MS5611_ADDRESS_ALT          0x76U   /* CSB 拉低 */

#define MS5611_CMD_RESET            0x1EU
#define MS5611_CMD_CONV_D1(osr)     ((uint8_t)(0x40U | (((osr) & 0x07U) << 1)))
#define MS5611_CMD_CONV_D2(osr)     ((uint8_t)(0x50U | (((osr) & 0x07U) << 1)))
#define MS5611_CMD_ADC_READ         0x00U
#define MS5611_CMD_PROM_READ(i)     ((uint8_t)(0xA0U | (((i) & 0x07U) << 1)))

#define MS5611_PROM_WORDS           8U
#define MS5611_PROM_DATA_SIZE       2U
#define MS5611_ADC_DATA_SIZE        3U

/* 过采样：OSR 0..5 对应 256/512/1024/2048/4096 次，默认 4096 */
#define MS5611_OSR_256              0x00U
#define MS5611_OSR_512              0x01U
#define MS5611_OSR_1024             0x02U
#define MS5611_OSR_2048             0x03U
#define MS5611_OSR_4096             0x04U
#define MS5611_OSR_COUNT            5U
#define MS5611_OSR_DEFAULT          MS5611_OSR_4096

/* 上电 / 复位 / 转换等待时间（ms，按 datasheet 最大值向上取整） */
#define MS5611_RESET_DELAY_MS       3U

/* 输出合理区间 */
#define MS5611_PRESSURE_MIN_PA      30000.0f
#define MS5611_PRESSURE_MAX_PA      110000.0f

#endif /* MS5611_REGISTER_H */
