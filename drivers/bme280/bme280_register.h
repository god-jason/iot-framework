#ifndef BME280_REGISTER_H
#define BME280_REGISTER_H

/*
 * BME280 寄存器 / 校准系数定义。
 *
 * 数据来源：Bosch BME280 datasheet。
 *   - chip id 0x60（寄存器 0xD0）
 *   - 校准系数分两组：0x88..0xA1（温压，26 字节）与 0xE1..0xE7（湿度，7 字节）
 *   - 数据 0xF7..0xFE：压力 3 + 温度 3 + 湿度 2 字节
 */

#define BME280_DEFAULT_ADDRESS      0x76U
#define BME280_ADDRESS_ALT          0x77U

#define BME280_REG_CALIB_00         0x88U   /* 0x88..0xA1，26 字节 */
#define BME280_REG_CHIP_ID          0xD0U
#define BME280_REG_RESET            0xE0U
#define BME280_REG_CALIB_HUM        0xE1U   /* 0xE1..0xE7，7 字节 */
#define BME280_REG_CTRL_HUM         0xF2U
#define BME280_REG_STATUS           0xF3U
#define BME280_REG_CTRL_MEAS        0xF4U
#define BME280_REG_CONFIG           0xF5U
#define BME280_REG_DATA             0xF7U   /* 8 字节 */

#define BME280_CHIP_ID              0x60U
#define BME280_RESET_VALUE          0xB6U

/* 过采样 */
#define BME280_OSRS_SKIP            0x00U
#define BME280_OSRS_X1              0x01U
#define BME280_OSRS_X2              0x02U
#define BME280_OSRS_X4              0x03U
#define BME280_OSRS_X8              0x04U
#define BME280_OSRS_X16             0x05U

/* 模式 */
#define BME280_MODE_SLEEP           0x00U
#define BME280_MODE_FORCED          0x01U
#define BME280_MODE_NORMAL          0x03U

/* ctrl_hum：湿度 x1 */
#define BME280_CTRL_HUM_DEFAULT     BME280_OSRS_X1
/* ctrl_meas：osrs_t = x2，osrs_p = x16，mode = normal */
#define BME280_CTRL_MEAS_DEFAULT    0x57U
/* config：t_sb = 500ms，filter = x16 */
#define BME280_CONFIG_DEFAULT       0x90U

#define BME280_RESET_DELAY_MS       5U

#define BME280_CALIB_DATA_SIZE      26U
#define BME280_CALIB_HUM_SIZE       7U
#define BME280_DATA_SIZE            8U

/* 12 位补码掩码（H4 / H5 符号扩展用） */
#define BME280_SIGN12_MASK          0x0800U
#define BME280_SIGN12_EXT           0xF000U

/* 输出合理区间 */
#define BME280_PRESSURE_MIN_PA      30000U
#define BME280_PRESSURE_MAX_PA      110000U

#endif /* BME280_REGISTER_H */
