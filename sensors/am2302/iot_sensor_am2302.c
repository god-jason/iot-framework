#include "iot_sensor_am2302.h"
#include "../dht22/iot_sensor_dht22.h"
#include "iot_logger.h"

int iot_am2302_init(const iot_gpio_bus_t *bus, uint8_t pin)
{
    /* AM2302 与 DHT22 时序一致，直接复用 */
    int r = iot_dht22_init(bus, pin);
    LOG_I("am2302", "init %s", r == 0 ? "ok" : "fail");
    return r;
}

int iot_am2302_read(const iot_gpio_bus_t *bus, uint8_t pin, float *temp, float *humidity)
{
    return iot_dht22_read(bus, pin, temp, humidity);
}