#ifndef BMP180_REGISTER_H
#define BMP180_REGISTER_H

/*
 * BMP180 寄存器 / 命令 / 校准系数定义。
 *
 * 数据来源：Bosch BMP180 datasheet（寄存器 0xD0 芯片 ID，0xAA 起 22 字节
 * 校准系数，0xF4 测量控制，0xF6 测量结果）。
 * 校准系数为 16 位大端；测量结果为 16 位（温度）或 19 位（压力，左对齐）。
 */

#define BMP180_DEFAULT_ADDRESS      0x77U   /* SDO 拉高 */
#define BMP180_ADDRESS_ALT          0x76U   /* SDO 拉低 */

#define BMP180_REG_CALIB_00         0xAAU   /* E2PROM 校准系数，共 22 字节 */
#define BMP180_REG_CHIP_ID          0xD0U   /* 固定 0x55 */
#define BMP180_REG_SOFT_RESET       0xE0U
#define BMP180_REG_CTRL_MEAS        0xF4U   /* 写入转换命令 */
#define BMP180_REG_ADC_OUT          0xF6U   /* 0xF6..0xF8，大端 */

#define BMP180_CHIP_ID              0x55U
#define BMP180_RESET_VALUE          0xB6U

/* 转换命令 */
#define BMP180_CMD_TEMP             0x2EU
#define BMP180_CMD_PRESS(oss)       ((uint8_t)(0x34U | (((oss) & 0x03U) << 6)))

/* 压力过采样（oss）：内部采样次数 1/2/4/8 */
#define BMP180_OSS_ULTRA_LOW        0x00U
#define BMP180_OSS_STANDARD         0x01U
#define BMP180_OSS_HIGH             0x02U
#define BMP180_OSS_ULTRA_HIGH       0x03U
#define BMP180_OSS_COUNT            4U
#define BMP180_OSS_DEFAULT          BMP180_OSS_HIGH

/* 转换时间（向上取整，ms），压力按 oss 查表 */
#define BMP180_CONV_DELAY_TEMP_MS   5U

/* 数据长度 */
#define BMP180_CALIB_DATA_SIZE      22U
#define BMP180_ADC_TEMP_SIZE        2U
#define BMP180_ADC_PRESS_SIZE       3U

/* 输出合理区间，用于剔除补偿失败的结果 */
#define BMP180_PRESSURE_MIN_PA      30000U
#define BMP180_PRESSURE_MAX_PA      110000U

#endif /* BMP180_REGISTER_H */
