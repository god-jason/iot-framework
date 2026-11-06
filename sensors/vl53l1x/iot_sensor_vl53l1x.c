#include "iot_sensor_vl53l1x.h"
#include "iot_logger.h"

#define VL53L1X_ADDR        0x29
#define VL53L1X_MODEL_ID_HI 0x01
#define VL53L1X_MODEL_ID_LO 0x0F
#define VL53L1X_MODE_VAL    0xEA
#define VL53L1X_RESULT_HI   0x00
#define VL53L1X_RESULT_LO   0x96

int iot_vl53l1x_init(const iot_i2c_bus_t *bus)
{
    uint8_t addr[2] = { VL53L1X_MODEL_ID_HI, VL53L1X_MODEL_ID_LO };
    uint8_t id = 0;

    if (bus == NULL || bus->write == NULL || bus->read == NULL) {
        return -1;
    }
    // VL53L1X 为 16 位寄存器地址，完整初始化含软件复位/距离模式/ROI 等，
    // 这里只给出器件探测骨架（读 0x010F model id 0xEA）
    if (bus->write(bus->ctx, VL53L1X_ADDR, addr, 2) != 0 ||
        bus->read(bus->ctx, VL53L1X_ADDR, &id, 1) != 0 ||
        id != VL53L1X_MODE_VAL) {
        LOG_E("vl53l1x", "probe fail 0x%02x", id);
        return -1;
    }
    LOG_I("vl53l1x", "init ok");
    return 0;
}

int iot_vl53l1x_read(const iot_i2c_bus_t *bus, uint16_t *distance_mm)
{
    uint8_t addr[2] = { VL53L1X_RESULT_HI, VL53L1X_RESULT_LO };
    uint8_t d[2];

    if (bus == NULL || bus->write == NULL || bus->read == NULL ||
        distance_mm == NULL) {
        return -1;
    }
    // 0x0096 为最终串扰校正距离结果（mm），骨架实现：直接读 2 字节大端
    if (bus->write(bus->ctx, VL53L1X_ADDR, addr, 2) != 0 ||
        bus->read(bus->ctx, VL53L1X_ADDR, d, 2) != 0) {
        return -1;
    }
    *distance_mm = (uint16_t)((d[0] << 8) | d[1]);
    return 0;
}