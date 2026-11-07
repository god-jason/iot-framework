#ifndef MPU6050_REGISTER_H
#define MPU6050_REGISTER_H

/* MPU6050 寄存器与常量（InvenSense RM-MPU-6000A-00） */

#define MPU6050_DEFAULT_ADDRESS         0x68U
#define MPU6050_ADDRESS_ALT             0x69U

/* 配置寄存器 */
#define MPU6050_REG_SMPLRT_DIV          0x19U
#define MPU6050_REG_CONFIG              0x1AU
#define MPU6050_REG_GYRO_CONFIG         0x1BU
#define MPU6050_REG_ACCEL_CONFIG        0x1CU
#define MPU6050_REG_INT_PIN_CFG         0x37U
#define MPU6050_REG_INT_ENABLE          0x38U
#define MPU6050_REG_INT_STATUS          0x3AU

/* 自检寄存器（出厂自检数据） */
#define MPU6050_REG_SELF_TEST_X         0x0DU
#define MPU6050_REG_SELF_TEST_Y         0x0EU
#define MPU6050_REG_SELF_TEST_Z         0x0FU
#define MPU6050_REG_SELF_TEST_A         0x10U

/* 数据输出寄存器，16 位大端，一次连续读 14 字节
 * ax, ay, az, temp, gx, gy, gz */
#define MPU6050_REG_ACCEL_XOUT_H        0x3BU
#define MPU6050_REG_TEMP_OUT_H          0x41U
#define MPU6050_REG_GYRO_XOUT_H         0x43U
#define MPU6050_DATA_BURST_LEN          14U

/* 电源管理 */
#define MPU6050_REG_PWR_MGMT_1          0x6BU
#define MPU6050_REG_PWR_MGMT_2          0x6CU
#define MPU6050_REG_WHO_AM_I            0x75U

/* WHO_AM_I：bit6:1 固定为 110100，bit0 与 bit7 为 0 */
#define MPU6050_WHO_AM_I_VALUE          0x68U
#define MPU6050_WHO_AM_I_MASK           0x7EU

/* PWR_MGMT_1 */
#define MPU6050_PWR1_DEVICE_RESET       0x80U
#define MPU6050_PWR1_SLEEP              0x40U
#define MPU6050_PWR1_CLKSEL_PLL_X       0x01U

/* 位域 */
#define MPU6050_GYRO_FS_MASK            0x18U
#define MPU6050_GYRO_FS_SHIFT           3U
#define MPU6050_ACCEL_FS_MASK           0x18U
#define MPU6050_ACCEL_FS_SHIFT          3U
#define MPU6050_CONFIG_DLPF_MASK        0x07U

/* INT_STATUS / INT_ENABLE */
#define MPU6050_INT_DATA_RDY            0x01U

/* 复位与上电延时 */
#define MPU6050_RESET_DELAY_MS          100U
#define MPU6050_WAKE_DELAY_MS           10U

/* 灵敏度：陀螺仪 LSB/(°/s) */
#define MPU6050_GYRO_LSB_PER_DPS_250    131.0f
#define MPU6050_GYRO_LSB_PER_DPS_500    65.5f
#define MPU6050_GYRO_LSB_PER_DPS_1000   32.8f
#define MPU6050_GYRO_LSB_PER_DPS_2000   16.4f

/* 灵敏度：加速度计 LSB/g */
#define MPU6050_ACCEL_LSB_PER_G_2G      16384.0f
#define MPU6050_ACCEL_LSB_PER_G_4G      8192.0f
#define MPU6050_ACCEL_LSB_PER_G_8G      4096.0f
#define MPU6050_ACCEL_LSB_PER_G_16G     2048.0f

/* 温度换算：T(°C) = raw / 340 + 36.53 */
#define MPU6050_TEMP_LSB_PER_DEGC       340.0f
#define MPU6050_TEMP_OFFSET_DEGC        36.53f

/* 采样率：DLPF 使能时陀螺仪输出基准为 1kHz */
#define MPU6050_SAMPLE_RATE_BASE_HZ     1000U
#define MPU6050_SMPLRT_DIV_MAX          255U

#endif /* MPU6050_REGISTER_H */
