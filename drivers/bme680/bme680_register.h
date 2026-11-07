#ifndef BME680_REGISTER_H
#define BME680_REGISTER_H

/*
 * BME680 寄存器 / 校准系数定义。
 *
 * 数据来源：Bosch BME680 datasheet。
 *   - chip id 0x61（寄存器 0xD0）
 *   - 校准系数两组：0x89..0x9C（温压，20 字节）与 0xE1..0xEA（湿度+温度，10 字节）
 *   - 数据 0x1D..0x26：压力 3 + 温度 3 + 湿度 2 + 气体 2 字节
 */

#define BME680_DEFAULT_ADDRESS      0x76U
#define BME680_ADDRESS_ALT          0x77U

#define BME680_REG_CHIP_ID          0xD0U
#define BME680_REG_RESET            0xE0U
#define BME680_REG_CALIB_00         0x89U   /* 0x89..0x9C，20 字节 */
#define BME680_REG_CALIB_01         0xE1U   /* 0xE1..0xEA，10 字节 */
/*
 * 数据区 0x1D..0x26（10 字节）：
 *   0x1D..0x1F 压力 20 位，0x20..0x22 温度 20 位，
 *   0x23..0x24 湿度 16 位，0x25..0x26 气体电阻 10 位。
 * 若实物湿度读数异常，请先核对此处寄存器映射。
 */
#define BME680_REG_DATA             0x1DU   /* 0x1D..0x26，10 字节 */

#define BME680_REG_CTRL_GAS_0       0x70U
#define BME680_REG_CTRL_GAS_1       0x71U
#define BME680_REG_CTRL_HUM         0x72U
#define BME680_REG_STATUS           0x73U
#define BME680_REG_CTRL_MEAS        0x74U
#define BME680_REG_CONFIG           0x75U

#define BME680_CHIP_ID              0x61U
#define BME680_RESET_VALUE          0xB6U

/* 过采样 */
#define BME680_OSRS_SKIP            0x00U
#define BME680_OSRS_X1              0x01U
#define BME680_OSRS_X2              0x02U
#define BME680_OSRS_X4              0x03U
#define BME680_OSRS_X8              0x04U
#define BME680_OSRS_X16             0x05U

/* 模式（ctrl_meas bit1:0） */
#define BME680_MODE_SLEEP           0x00U
#define BME680_MODE_FORCED          0x01U

/* ctrl_meas：osrs_t = x2，osrs_p = x16，forced */
#define BME680_CTRL_MEAS_FORCED     ((uint8_t)((BME680_OSRS_X2 << 5) | \
                                               (BME680_OSRS_X16 << 2) | \
                                               BME680_MODE_FORCED))
/* ctrl_hum：湿度 x1 */
#define BME680_CTRL_HUM_DEFAULT     BME680_OSRS_X1
/* config：IIR 滤波关闭 */
#define BME680_CONFIG_DEFAULT       0x00U
/* ctrl_gas_1：关闭气体测量（run_gas = 0，nb_conv = 0） */
#define BME680_CTRL_GAS1_OFF        0x00U

#define BME680_RESET_DELAY_MS       5U
#define BME680_MEASURE_DELAY_MS     45U

#define BME680_CALIB1_SIZE          20U
#define BME680_CALIB2_SIZE          10U
#define BME680_DATA_SIZE            10U

/* 输出合理区间 */
#define BME680_PRESSURE_MIN_PA      30000.0f
#define BME680_PRESSURE_MAX_PA      110000.0f

#endif /* BME680_REGISTER_H */
