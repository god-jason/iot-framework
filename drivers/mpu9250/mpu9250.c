#include "mpu9250.h"
#include "iot_logger.h"

#define MPU9250_ADDR         0x68
#define MPU9250_REG_WHO_AM_I 0x75
#define MPU9250_REG_PWR_MGMT 0x6B
#define MPU9250_REG_ACCEL_X  0x3B

/* MPU9250 的 WHO_AM_I 可能为 0x71 或 0x73 */
#define MPU9250_WHO_AM_I_VAL_A 0x71
#define MPU9250_WHO_AM_I_VAL_B 0x73

int mpu9250_init(const i2c_bus_t *bus)
{
    uint8_t id = 0;

    if (bus == NULL) {
        LOG_E("mpu9250", "bus null");
        return -1;
    }
    if (i2c_read_reg(bus, MPU9250_ADDR, MPU9250_REG_WHO_AM_I, &id) != 0) {
        LOG_E("mpu9250", "read WHO_AM_I fail");
        return -1;
    }
    if (id != MPU9250_WHO_AM_I_VAL_A && id != MPU9250_WHO_AM_I_VAL_B) {
        LOG_E("mpu9250", "bad id 0x%02X", id);
        return -1;
    }
    /* 唤醒设备：PWR_MGMT_1 = 0 */
    if (i2c_write_reg(bus, MPU9250_ADDR, MPU9250_REG_PWR_MGMT, 0x00) != 0) {
        LOG_E("mpu9250", "write PWR_MGMT fail");
        return -1;
    }
    LOG_I("mpu9250", "init ok");
    return 0;
}

int mpu9250_read(const i2c_bus_t *bus, int16_t acc[3], int16_t gyro[3])
{
    uint8_t d[14];

    if (bus == NULL || acc == NULL || gyro == NULL) {
        return -1;
    }
    if (i2c_read_regs(bus, MPU9250_ADDR, MPU9250_REG_ACCEL_X, d, 14) != 0) {
        LOG_E("mpu9250", "read fail");
        return -1;
    }
    /* 大端解析：0x3B 起 14 字节为 ax,ay,az,temp,gx,gy,gz */
    acc[0] = (int16_t)((d[0] << 8) | d[1]);
    acc[1] = (int16_t)((d[2] << 8) | d[3]);
    acc[2] = (int16_t)((d[4] << 8) | d[5]);
    gyro[0] = (int16_t)((d[8] << 8) | d[9]);
    gyro[1] = (int16_t)((d[10] << 8) | d[11]);
    gyro[2] = (int16_t)((d[12] << 8) | d[13]);
    return 0;
}