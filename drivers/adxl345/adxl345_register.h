#ifndef ADXL345_REGISTER_H
#define ADXL345_REGISTER_H

/* ADXL345 寄存器定义 */

#define ADXL345_DEFAULT_ADDRESS     0x53U
#define ADXL345_ADDRESS_ALT         0x1DU

#define ADXL345_REG_DEVID           0x00U
#define ADXL345_REG_THRESH_TAP      0x1DU
#define ADXL345_REG_OFSX            0x1EU
#define ADXL345_REG_OFSY            0x1FU
#define ADXL345_REG_OFSZ            0x20U
#define ADXL345_REG_BW_RATE         0x2CU
#define ADXL345_REG_POWER_CTL       0x2DU
#define ADXL345_REG_INT_ENABLE      0x2EU
#define ADXL345_REG_INT_MAP         0x2FU
#define ADXL345_REG_INT_SOURCE      0x30U
#define ADXL345_REG_DATA_FORMAT     0x31U
#define ADXL345_REG_DATAX0          0x32U
#define ADXL345_REG_DATAX1          0x33U
#define ADXL345_REG_DATAY0          0x34U
#define ADXL345_REG_DATAY1          0x35U
#define ADXL345_REG_DATAZ0          0x36U
#define ADXL345_REG_DATAZ1          0x37U
#define ADXL345_REG_FIFO_CTL        0x38U
#define ADXL345_REG_FIFO_STATUS     0x39U

#define ADXL345_DEVID_VALUE         0xE5U

/* POWER_CTL */
#define ADXL345_POWER_MEASURE       0x08U
#define ADXL345_POWER_STANDBY       0x00U

/* DATA_FORMAT */
#define ADXL345_FORMAT_FULL_RES     0x08U
#define ADXL345_RANGE_2G            0x00U
#define ADXL345_RANGE_4G            0x01U
#define ADXL345_RANGE_8G            0x02U
#define ADXL345_RANGE_16G           0x03U

/* BW_RATE：100Hz 输出数据率 */
#define ADXL345_BW_RATE_100HZ       0x0AU

/* 全分辨率固定灵敏度 */
#define ADXL345_SCALE_G_PER_LSB     0.0039f

#endif /* ADXL345_REGISTER_H */
