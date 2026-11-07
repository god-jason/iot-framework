#ifndef BMI270_REGISTER_H
#define BMI270_REGISTER_H

/* BMI270 六轴 IMU 寄存器 / 量程 / 输出率定义（I2C，数据小端）。 */

#define BMI270_DEFAULT_ADDRESS      0x68U   /* SDO 接 GND */
#define BMI270_ADDRESS_ALT          0x69U   /* SDO 接 VDDIO */

/* ---------------- 寄存器 ---------------- */
#define BMI270_REG_CHIP_ID          0x00U
#define BMI270_REG_ERR              0x02U
#define BMI270_REG_STATUS           0x03U

/* 数据寄存器 0x0C ~ 0x17：加速度 XYZ、陀螺 XYZ（各 16 位小端） */
#define BMI270_REG_ACC_X_L          0x0CU
#define BMI270_REG_ACC_X_H          0x0DU
#define BMI270_REG_ACC_Y_L          0x0EU
#define BMI270_REG_ACC_Y_H          0x0FU
#define BMI270_REG_ACC_Z_L          0x10U
#define BMI270_REG_ACC_Z_H          0x11U
#define BMI270_REG_GYR_X_L          0x12U
#define BMI270_REG_GYR_X_H          0x13U
#define BMI270_REG_GYR_Y_L          0x14U
#define BMI270_REG_GYR_Y_H          0x15U
#define BMI270_REG_GYR_Z_L          0x16U
#define BMI270_REG_GYR_Z_H          0x17U

#define BMI270_REG_INTERNAL_STATUS  0x21U
#define BMI270_REG_ACC_CONF         0x40U
#define BMI270_REG_ACC_RANGE        0x41U
#define BMI270_REG_GYR_CONF         0x42U
#define BMI270_REG_GYR_RANGE        0x43U

/* 配置固件加载接口（本驱动不加载固件，仅保留定义便于扩展） */
#define BMI270_REG_INIT_CTRL        0x59U
#define BMI270_REG_INIT_ADDR_0      0x5BU
#define BMI270_REG_INIT_ADDR_1      0x5CU
#define BMI270_REG_INIT_DATA        0x5EU

#define BMI270_REG_FEAT_PAGE        0x2FU
#define BMI270_REG_PWR_CONF         0x7CU
#define BMI270_REG_PWR_CTRL         0x7DU
#define BMI270_REG_CMD              0x7EU

/* ---------------- 器件标识 ---------------- */
#define BMI270_CHIP_ID_VALUE        0x24U
/* INTERNAL_STATUS.msg：0x01 表示配置固件已加载并进入应用模式 */
#define BMI270_INTERNAL_STATUS_MSG_MASK  0x0FU
#define BMI270_INTERNAL_STATUS_INIT_OK   0x01U

/* ---------------- PWR_CONF / PWR_CTRL ---------------- */
#define BMI270_PWR_CONF_PERF        0x00U   /* 关闭高级省电，进入性能模式 */
#define BMI270_PWR_CTRL_ACC_EN      0x04U
#define BMI270_PWR_CTRL_GYR_EN      0x02U
#define BMI270_PWR_CTRL_AUX_EN      0x01U
#define BMI270_PWR_CTRL_ACC_GYR     (BMI270_PWR_CTRL_ACC_EN | BMI270_PWR_CTRL_GYR_EN)

/* ---------------- ACC_CONF / GYR_CONF 位域 ----------------
 * ACC_CONF(0x40)：bit7 acc_filter_perf，bit6:4 acc_bwp，bit3:0 acc_odr
 * GYR_CONF(0x42)：bit7 gyr_filter_perf，bit6 gyr_noise_perf，bit5:4 gyr_bwp，bit3:0 gyr_odr
 */
#define BMI270_ACC_FILTER_PERF      0x80U
#define BMI270_GYR_FILTER_PERF      0x80U
#define BMI270_GYR_NOISE_PERF       0x40U
#define BMI270_ACC_BWP_NORMAL       0x02U   /* 抽取率 = 2（默认带宽） */
#define BMI270_GYR_BWP_NORMAL       0x02U

#define BMI270_ACC_CONF_VALUE(bwp, odr) \
    ((uint8_t)(BMI270_ACC_FILTER_PERF | (((bwp) & 0x07U) << 4) | ((odr) & 0x0FU)))
#define BMI270_GYR_CONF_VALUE(bwp, odr) \
    ((uint8_t)(BMI270_GYR_FILTER_PERF | BMI270_GYR_NOISE_PERF | \
               (((bwp) & 0x03U) << 4) | ((odr) & 0x0FU)))

/* ---------------- 输出率编码（ACC_CONF[3:0] / GYR_CONF[3:0]） ---------------- */
#define BMI270_ODR_CODE_25HZ        0x06U
#define BMI270_ODR_CODE_50HZ        0x07U
#define BMI270_ODR_CODE_100HZ       0x08U
#define BMI270_ODR_CODE_200HZ       0x09U
#define BMI270_ODR_CODE_400HZ       0x0AU
#define BMI270_ODR_CODE_800HZ       0x0BU
#define BMI270_ODR_CODE_1600HZ      0x0CU

/* ---------------- ACC_RANGE 编码 ---------------- */
#define BMI270_ACC_RANGE_CODE_2G    0x00U
#define BMI270_ACC_RANGE_CODE_4G    0x01U
#define BMI270_ACC_RANGE_CODE_8G    0x02U
#define BMI270_ACC_RANGE_CODE_16G   0x03U

/* ---------------- GYR_RANGE 编码 ---------------- */
#define BMI270_GYR_RANGE_CODE_2000DPS  0x00U
#define BMI270_GYR_RANGE_CODE_1000DPS  0x01U
#define BMI270_GYR_RANGE_CODE_500DPS   0x02U
#define BMI270_GYR_RANGE_CODE_250DPS   0x03U
#define BMI270_GYR_RANGE_CODE_125DPS   0x04U

/* ---------------- 灵敏度（LSB 每物理单位） ---------------- */
#define BMI270_ACC_LSB_PER_G_2G     16384.0f
#define BMI270_ACC_LSB_PER_G_4G      8192.0f
#define BMI270_ACC_LSB_PER_G_8G      4096.0f
#define BMI270_ACC_LSB_PER_G_16G     2048.0f

#define BMI270_GYR_LSB_PER_DPS_2000  16.384f
#define BMI270_GYR_LSB_PER_DPS_1000  32.768f
#define BMI270_GYR_LSB_PER_DPS_500   65.536f
#define BMI270_GYR_LSB_PER_DPS_250  131.072f
#define BMI270_GYR_LSB_PER_DPS_125  262.144f

/* 上电 / 启动时序（数据手册） */
#define BMI270_POWER_UP_DELAY_US    450U
#define BMI270_START_UP_DELAY_MS    50U

#endif /* BMI270_REGISTER_H */
