#include "vl53l0x.h"
#include "iot_logger.h"

#define VL53L0X_ADDR         0x29
#define VL53L0X_MODEL_ID     0xC0
#define VL53L0X_MODEL_VAL    0xEE
#define VL53L0X_RESULT_REG   0x1E

int vl53l0x_init(const i2c_bus_t *bus)
{
    uint8_t id = 0;

    if (bus == NULL) {
        return -1;
    }
    // 完整初始化需写入参考 SPAD/校准/测距时序等长序列，
    // 这里只给出器件探测骨架（读取 model id 0xEE）
    if (i2c_read_reg(bus, VL53L0X_ADDR, VL53L0X_MODEL_ID, &id) != 0 ||
        id != VL53L0X_MODEL_VAL) {
        LOG_E("vl53l0x", "probe fail 0x%02x", id);
        return -1;
    }
    LOG_I("vl53l0x", "init ok");
    return 0;
}

int vl53l0x_read(const i2c_bus_t *bus, uint16_t *distance_mm)
{
    uint8_t d[2];

    if (bus == NULL || distance_mm == NULL) {
        return -1;
    }
    // 骨架实现：真实距离需先发起测距并轮询状态，结果寄存器位于 0x1E 附近
    if (i2c_read_regs(bus, VL53L0X_ADDR, VL53L0X_RESULT_REG, d, 2) != 0) {
        return -1;
    }
    // 简化假设结果为大端 16 位
    *distance_mm = (uint16_t)((d[0] << 8) | d[1]);
    return 0;
}