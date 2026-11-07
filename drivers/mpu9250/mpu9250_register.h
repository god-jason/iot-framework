#ifndef MPU9250_REGISTER_H
#define MPU9250_REGISTER_H

/* MPU9250（IMU 部分同 MPU6500）+ AK8963 磁力计 寄存器定义 */

#define MPU9250_DEFAULT_ADDRESS         0x68U
#define MPU9250_ADDRESS_ALT             0x69U
#define MPU9250_WHO_AM_I_VALUE          0x71U

/* ---- MPU9250 内部 IMU 寄存器 ---- */
#define MPU9250_REG_SMPLRT_DIV          0x19U
#define MPU9250_REG_CONFIG              0x1AU
#define MPU9250_REG_GYRO_CONFIG         0x1BU
#define MPU9250_REG_ACCEL_CONFIG        0x1CU
#define MPU9250_REG_ACCEL_CONFIG2       0x1DU
#define MPU9250_REG_INT_PIN_CFG         0x37U
#define MPU9250_REG_INT_ENABLE          0x38U
#define MPU9250_REG_INT_STATUS          0x3AU
#define MPU9250_REG_USER_CTRL           0x6AU
#define MPU9250_REG_PWR_MGMT_1          0x6BU
#define MPU9250_REG_WHO_AM_I            0x75U

/* 0x3B 起 14 字节：ax, ay, az, temp, gx, gy, gz（大端） */
#define MPU9250_REG_ACCEL_XOUT_H        0x3BU
#define MPU9250_DATA_BURST_LEN          14U
#define MPU9250_BURST_MAX               16U

/* INT_PIN_CFG：bit1 旁路使能，主机可直接访问 AK8963 */
#define MPU9250_INT_PIN_BYPASS_EN       0x02U

/* USER_CTRL：bit4 关闭 I2C 从口（SPI 模式使用） */
#define MPU9250_USER_I2C_IF_DIS         0x10U

/* PWR_MGMT_1 */
#define MPU9250_PWR1_DEVICE_RESET       0x80U
#define MPU9250_PWR1_SLEEP              0x40U
#define MPU9250_PWR1_CLKSEL_PLL_X       0x01U

/* 位域 */
#define MPU9250_GYRO_FS_MASK            0x18U
#define MPU9250_GYRO_FS_SHIFT           3U
#define MPU9250_ACCEL_FS_MASK           0x18U
#define MPU9250_ACCEL_FS_SHIFT          3U
#define MPU9250_DLPF_41HZ               0x03U
#define MPU9250_INT_DATA_RDY            0x01U

/* ---- AK8963 磁力计（仅 I2C 旁路模式可访问） ---- */
#define AK8963_ADDRESS                  0x0CU
#define AK8963_REG_WIA                  0x00U
#define AK8963_REG_INFO                 0x01U
#define AK8963_REG_ST1                  0x02U
#define AK8963_REG_HXL                  0x03U
#define AK8963_REG_ST2                  0x09U
#define AK8963_REG_CNTL1                0x0AU
#define AK8963_REG_CNTL2                0x0BU
#define AK8963_REG_ASAX                 0x10U
#define AK8963_WIA_VALUE                0x48U
#define AK8963_CNTL2_SOFT_RESET         0x01U
#define AK8963_CNTL1_POWER_DOWN         0x00U
#define AK8963_CNTL1_FUSE_ROM           0x0FU
#define AK8963_CNTL1_16BIT_100HZ        0x16U
#define AK8963_ST1_DRDY                 0x01U
#define AK8963_ST2_HOFL                 0x08U
#define AK8963_ASA_COUNT                3U
/* ST1 + X/Y/Z(6) + ST2 */
#define AK8963_MAG_BURST_LEN            8U
/* 16 位输出模式分辨率：0.15 µT/LSB（需按 ASA 修正） */
#define AK8963_UT_PER_LSB               0.15f

/* ---- 灵敏度 ---- */
#define MPU9250_GYRO_LSB_PER_DPS_250    131.0f
#define MPU9250_GYRO_LSB_PER_DPS_500    65.5f
#define MPU9250_GYRO_LSB_PER_DPS_1000   32.8f
#define MPU9250_GYRO_LSB_PER_DPS_2000   16.4f
#define MPU9250_ACCEL_LSB_PER_G_2G      16384.0f
#define MPU9250_ACCEL_LSB_PER_G_4G      8192.0f
#define MPU9250_ACCEL_LSB_PER_G_8G      4096.0f
#define MPU9250_ACCEL_LSB_PER_G_16G     2048.0f

/* 温度：T(°C) = raw / 333.87 + 21.0 */
#define MPU9250_TEMP_LSB_PER_DEGC       333.87f
#define MPU9250_TEMP_OFFSET_DEGC        21.0f

/* 延时与超时 */
#define MPU9250_RESET_DELAY_MS          100U
#define MPU9250_WAKE_DELAY_MS           10U
#define MPU9250_MAG_START_DELAY_MS      10U
#define MPU9250_MAG_TIMEOUT_MS          20U

/* 采样率 */
#define MPU9250_SAMPLE_RATE_BASE_HZ     1000U
#define MPU9250_SMPLRT_DIV_MAX          255U

#endif /* MPU9250_REGISTER_H */
