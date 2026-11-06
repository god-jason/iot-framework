#include "iot_sensor_vl6180x.h"
#include "iot_logger.h"

#define VL6180X_ADDR       0x29
#define VL6180X_MODEL_ID   0x00
#define VL6180X_MODEL_VAL  0xB4
#define VL6180X_RESULT_REG 0x62

int iot_vl6180x_init(const iot_i2c_bus_t *bus)
{
    uint8_t id = 0;

    if (bus == NULL) {
        return -1;
    }
    // 完整初始化需装载默认设置并配置测距，这里只给出探测骨架（model id 0xB4）
    if (iot_i2c_read_reg(bus, VL6180X_ADDR, VL6180X_MODEL_ID, &id) != 0 ||
        id != VL6180X_MODEL_VAL) {
        LOG_E("vl6180x", "probe fail 0x%02x", id);
        return -1;
    }
    LOG_I("vl6180x", "init ok");
    return 0;
}

int iot_vl6180x_read(const iot_i2c_bus_t *bus, uint16_t *distance_mm)
{
    uint8_t val = 0;

    if (bus == NULL || distance_mm == NULL) {
        return -1;
    }
    // 骨架实现：真实距离需先启动测距并轮询结果；0x62 为单字节距离结果（mm）
    if (iot_i2c_read_reg(bus, VL6180X_ADDR, VL6180X_RESULT_REG, &val) != 0) {
        return -1;
    }
    *distance_mm = (uint16_t)val;
    return 0;
}