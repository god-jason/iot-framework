#include "iot_sensor_mlx90640.h"
#include "iot_logger.h"

#define MLX90640_ADDR      0x33
#define MLX90640_RAM_START 0x0400  // 像素 RAM 起始（16 位地址）

int iot_mlx90640_init(const iot_i2c_bus_t *bus)
{
    uint8_t reg[2] = { 0x04, 0x00 };
    uint8_t d[2];

    if (bus == NULL || bus->write == NULL || bus->read == NULL) {
        return -1;
    }
    // MLX90640 为 16 位寄存器地址：EEPROM（0x2400 起 832 字节）保存每像素
    // 校准系数（offset/增益/KvTa 等），真实换算需读取并做补偿；
    // 这里只做 I2C 探测骨架（读 0x0400 像素 RAM）
    if (bus->write(bus->ctx, MLX90640_ADDR, reg, 2) != 0 ||
        bus->read(bus->ctx, MLX90640_ADDR, d, 2) != 0) {
        LOG_E("mlx90640", "probe fail");
        return -1;
    }
    LOG_I("mlx90640", "init ok");
    return 0;
}

int iot_mlx90640_read_pixel(const iot_i2c_bus_t *bus, uint16_t addr, float *temp)
{
    uint16_t ram_addr;
    uint8_t  reg[2];
    uint8_t  d[2];
    uint16_t raw;

    if (bus == NULL || bus->write == NULL || bus->read == NULL ||
        temp == NULL || addr >= IOT_MLX90640_PIXELS) {
        return -1;
    }
    // 每像素 2 字节，RAM 地址 = 0x0400 + 序号 * 2
    ram_addr = MLX90640_RAM_START + addr * 2;
    reg[0] = (uint8_t)(ram_addr >> 8);
    reg[1] = (uint8_t)(ram_addr & 0xFF);
    if (bus->write(bus->ctx, MLX90640_ADDR, reg, 2) != 0 ||
        bus->read(bus->ctx, MLX90640_ADDR, d, 2) != 0) {
        return -1;
    }
    // 假设大端，低 2 位为状态标志，高 14 位为原始数据
    raw = (uint16_t)((d[0] << 8) | d[1]);
    raw >>= 2;
    // 简化线性估算：精确温度需用 EEPROM 校准系数 + 环境温度补偿
    *temp = (float)raw * 0.25f - 273.15f;
    return 0;
}

int iot_mlx90640_read(const iot_i2c_bus_t *bus, float *frame, uint16_t len)
{
    uint16_t i;

    if (bus == NULL || frame == NULL || len == 0 || len > IOT_MLX90640_PIXELS) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (iot_mlx90640_read_pixel(bus, i, &frame[i]) != 0) {
            return -1;
        }
    }
    return 0;
}