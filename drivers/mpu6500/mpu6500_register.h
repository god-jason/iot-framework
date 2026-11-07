#ifndef MPU6500_REGISTER_H
#define MPU6500_REGISTER_H

/* MPU6500 寄存器与常量（InvenSense RM-MPU-6500A-00） */

#define MPU6500_DEFAULT_ADDRESS         0x68U
#define MPU6500_ADDRESS_ALT             0x69U

/* 配置寄存器 */
#define MPU6500_REG_SMPLRT_DIV          0x19U
#define MPU6500_REG_CONFIG              0x1AU
#define MPU6500_REG_GYRO_CONFIG         0x1BU
#define MPU6500_REG_ACCEL_CONFIG        0x1CU
#define MPU6500_REG_ACCEL_CONFIG2       0x1DU
#define MPU6500_REG_INT_PIN_CFG         0x37U
#define MPU6500_REG_INT_ENABLE          0x38U
#define MPU6500_REG_INT_STATUS          0x3AU

/* 数据输出：0x3B 起 14 字节，ax, ay, az, temp, gx, gy, gz（大端） */
#define MPU6500_REG_ACCEL_XOUT_H        0x3BU
#define MPU6500_REG_TEMP_OUT_H          0x41U
#define MPU6500_REG_GYRO_XOUT_H         0x43U
#define MPU6500_DATA_BURST_LEN          14U
#define MPU6500_BURST_MAX               16U

/* 用户控制 / 电源管理 */
#define MPU6500_REG_USER_CTRL           0x6AU
#define MPU6500_REG_PWR_MGMT_1          0x6BU
#define MPU6500_REG_PWR_MGMT_2          0x6CU
#define MPU6500_REG_WHO_AM_I            0x75U

#define MPU6500_USER_I2C_IF_DIS         0x10U   /* SPI 模式下关闭 I2C 从口 */

/* WHO_AM_I：0x70（MPU6500）；0x71/0x73 为共用同一 IMU 核的 MPU9250/9255 */
#define MPU6500_WHO_AM_I_VALUE          0x70U

/* PWR_MGMT_1 */
#define MPU6500_PWR1_DEVICE_RESET       0x80U
#define MPU6500_PWR1_SLEEP              0x40U
#define MPU6500_PWR1_CLKSEL_PLL_X       0x01U

/* 位域 */
#define MPU6500_GYRO_FS_MASK            0x18U
#define MPU6500_GYRO_FS_SHIFT           3U
#define MPU6500_ACCEL_FS_MASK           0x18U
#define MPU6500_ACCEL_FS_SHIFT          3U

/* DLPF 配置：0x03 ≈ 41Hz 带宽 */
#define MPU6500_DLPF_41HZ               0x03U

/* INT_STATUS / INT_ENABLE：bit0 数据就绪 */
#define MPU6500_INT_DATA_RDY            0x01U

/* 复位与唤醒延时 */
#define MPU6500_RESET_DELAY_MS          100U
#define MPU6500_WAKE_DELAY_MS           10U

/* 灵敏度：陀螺仪 LSB/(°/s) */
#define MPU6500_GYRO_LSB_PER_DPS_250    131.0f
#define MPU6500_GYRO_LSB_PER_DPS_500    65.5f
#define MPU6500_GYRO_LSB_PER_DPS_1000   32.8f
#define MPU6500_GYRO_LSB_PER_DPS_2000   16.4f

/* 灵敏度：加速度计 LSB/g */
#define MPU6500_ACCEL_LSB_PER_G_2G      16384.0f
#define MPU6500_ACCEL_LSB_PER_G_4G      8192.0f
#define MPU6500_ACCEL_LSB_PER_G_8G      4096.0f
#define MPU6500_ACCEL_LSB_PER_G_16G     2048.0f

/* 温度换算：T(°C) = raw / 333.87 + 21.0 */
#define MPU6500_TEMP_LSB_PER_DEGC       333.87f
#define MPU6500_TEMP_OFFSET_DEGC        21.0f

/* 采样率：DLPF 使能时基准 1kHz */
#define MPU6500_SAMPLE_RATE_BASE_HZ     1000U
#define MPU6500_SMPLRT_DIV_MAX          255U

#endif /* MPU6500_REGISTER_H */
