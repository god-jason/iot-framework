#ifndef BMP280_REGISTER_H
#define BMP280_REGISTER_H

/* BMP280 寄存器定义 */

#define BMP280_DEFAULT_ADDRESS      0x76U
#define BMP280_ADDRESS_ALT          0x77U

#define BMP280_REG_CALIB_00         0x88U
#define BMP280_REG_CHIP_ID          0xD0U
#define BMP280_REG_RESET            0xE0U
#define BMP280_REG_STATUS           0xF3U
#define BMP280_REG_CTRL_MEAS        0xF4U
#define BMP280_REG_CONFIG           0xF5U
#define BMP280_REG_PRESS_MSB        0xF7U
#define BMP280_REG_PRESS_LSB        0xF8U
#define BMP280_REG_PRESS_XLSB       0xF9U
#define BMP280_REG_TEMP_MSB         0xFAU
#define BMP280_REG_TEMP_LSB         0xFBU
#define BMP280_REG_TEMP_XLSB        0xFCU

#define BMP280_CHIP_ID              0x58U
#define BMP280_RESET_VALUE          0xB6U

/* 过采样与模式 */
#define BMP280_OSRS_SKIP            0x00U
#define BMP280_OSRS_X1              0x01U
#define BMP280_OSRS_X2              0x02U
#define BMP280_OSRS_X4              0x03U
#define BMP280_OSRS_X8              0x04U
#define BMP280_OSRS_X16             0x05U

#define BMP280_MODE_SLEEP           0x00U
#define BMP280_MODE_FORCED          0x01U
#define BMP280_MODE_NORMAL          0x03U

/* ctrl_meas: osrs_t= x2, osrs_p= x16, mode= normal -> 0x57 */
#define BMP280_CTRL_MEAS_DEFAULT    0x57U
/* config: t_sb=500ms, filter=x16 -> 0x90 */
#define BMP280_CONFIG_DEFAULT       0x90U

/* 转换时间（按最大过采样估算，ms） */
#define BMP280_MEASURE_DELAY_MS     40U

#define BMP280_CALIB_DATA_SIZE      24U

#endif /* BMP280_REGISTER_H */
