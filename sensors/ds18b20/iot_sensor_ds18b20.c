#include "iot_sensor_ds18b20.h"
#include "iot_logger.h"

#define DS18B20_CMD_SKIP_ROM  0xCC  /* 跳过 ROM */
#define DS18B20_CMD_CONVERT   0x44  /* 启动温度转换 */
#define DS18B20_CMD_READ_SP   0xBE  /* 读 scratchpad */

int iot_ds18b20_start(const iot_onewire_bus_t *bus)
{
    if (iot_onewire_reset(bus) != 0) {
        LOG_E("ds18b20", "reset fail");
        return -1;
    }
    iot_onewire_write_byte(bus, DS18B20_CMD_SKIP_ROM);
    iot_onewire_write_byte(bus, DS18B20_CMD_CONVERT);
    LOG_I("ds18b20", "convert start");
    return 0;
}

int iot_ds18b20_read(const iot_onewire_bus_t *bus, float *celsius)
{
    uint8_t d[9];
    int16_t raw;
    int i;

    if (celsius == NULL) {
        return -1;
    }
    if (iot_onewire_reset(bus) != 0) {
        LOG_E("ds18b20", "reset fail");
        return -1;
    }
    iot_onewire_write_byte(bus, DS18B20_CMD_SKIP_ROM);
    iot_onewire_write_byte(bus, DS18B20_CMD_READ_SP);
    for (i = 0; i < 9; i++) {
        d[i] = iot_onewire_read_byte(bus);
    }
    /* 前 2 字节小端 16bit，单位 1/16 摄氏度 */
    raw = (int16_t)(d[0] | (d[1] << 8));
    *celsius = raw / 16.0f;
    return 0;
}