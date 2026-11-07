#include "am2302.h"
#include "../dht22/dht22.h"
#include "iot_logger.h"

int am2302_init(const gpio_bus_t *bus, uint8_t pin)
{
    /* AM2302 与 DHT22 时序一致，直接复用 */
    int r = dht22_init(bus, pin);
    LOG_I("am2302", "init %s", r == 0 ? "ok" : "fail");
    return r;
}

int am2302_read(const gpio_bus_t *bus, uint8_t pin, float *temp, float *humidity)
{
    return dht22_read(bus, pin, temp, humidity);
}