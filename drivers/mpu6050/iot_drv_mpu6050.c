#include "iot_drv_mpu6050.h"
#include "iot_logger.h"

#define MPU6050_ADDR          0x68
#define MPU6050_REG_WHO_AM_I  0x75
#define MPU6050_REG_PWR_MGMT  0x6B
#define MPU6050_REG_SMPLRT    0x19
#define MPU6050_REG_GYRO_CFG  0x1B
#define MPU6050_REG_ACCEL_CFG 0x1C
#define MPU6050_REG_ACCEL_XOUT 0x3B

#define MPU6050_WHO_AM_I_VAL  0x68

int iot_mpu6050_init(const iot_i2c_bus_t *bus)
{
    uint8_t id = 0;

    if (bus == NULL) {
        LOG_E("mpu6050", "bus null");
        return -1;
    }
    /* WHO_AM_I 应读回 0x68 */
    if (iot_i2c_read_reg(bus, MPU6050_ADDR, MPU6050_REG_WHO_AM_I, &id) != 0) {
        LOG_E("mpu6050", "read WHO_AM_I fail");
        return -1;
    }
    if (id != MPU6050_WHO_AM_I_VAL) {
        LOG_E("mpu6050", "bad id 0x%02X", id);
        return -1;
    }
    /* 唤醒设备：PWR_MGMT_1 = 0 */
    iot_i2c_write_reg(bus, MPU6050_ADDR, MPU6050_REG_PWR_MGMT, 0x00);
    /* 样本率分频 = 7 */
    iot_i2c_write_reg(bus, MPU6050_ADDR, MPU6050_REG_SMPLRT, 0x07);
    /* 加速度计满量程 ±2g（FS_SEL=0） */
    iot_i2c_write_reg(bus, MPU6050_ADDR, MPU6050_REG_ACCEL_CFG, 0x00);
    /* 陀螺仪满量程 ±250 dps（FS_SEL=0） */
    iot_i2c_write_reg(bus, MPU6050_ADDR, MPU6050_REG_GYRO_CFG, 0x00);

    LOG_I("mpu6050", "init ok");
    return 0;
}

int iot_mpu6050_read(const iot_i2c_bus_t *bus, int16_t acc[3], int16_t gyro[3])
{
    uint8_t d[14];

    if (bus == NULL || acc == NULL || gyro == NULL) {
        return -1;
    }
    if (iot_i2c_read_regs(bus, MPU6050_ADDR, MPU6050_REG_ACCEL_XOUT, d, 14) != 0) {
        LOG_E("mpu6050", "read fail");
        return -1;
    }
    /* 大端解析：高字节在前。0x3B 起 14 字节为 ax,ay,az,temp,gx,gy,gz */
    acc[0] = (int16_t)((d[0] << 8) | d[1]);
    acc[1] = (int16_t)((d[2] << 8) | d[3]);
    acc[2] = (int16_t)((d[4] << 8) | d[5]);
    gyro[0] = (int16_t)((d[8] << 8) | d[9]);
    gyro[1] = (int16_t)((d[10] << 8) | d[11]);
    gyro[2] = (int16_t)((d[12] << 8) | d[13]);
    return 0;
}