#include "am2302.h"
#include "am2302_register.h"
#include "log.h"

/*
 * AM2302 与 DHT22 时序、帧格式完全一致，全部实现放在 dht22.c，
 * 这里只做参数校验与转发，保证只有一份位时序代码。
 *
 * 编译期确认帧长与底层 DHT22 定义一致，防止两侧寄存器定义漂移。
 */
typedef char am2302_frame_bytes_check[(AM2302_FRAME_BYTES == 5U) ? 1 : -1];

int am2302_init(am2302_handle_t *handle, hal_pin_t pin)
{
    int ret;

    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = dht22_init(&handle->dev, pin);
    if (ret != DRIVER_OK) {
        LOG_E("am2302", "init failed: %d", ret);
        return ret;
    }
    LOG_I("am2302", "init ok, pin %u", (unsigned int)pin);
    return DRIVER_OK;
}

int am2302_deinit(am2302_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return dht22_deinit(&handle->dev);
}

int am2302_read(am2302_handle_t *handle, float *temperature, float *humidity)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return dht22_read(&handle->dev, temperature, humidity);
}

int am2302_read_raw(am2302_handle_t *handle, uint8_t frame[5])
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return dht22_read_raw(&handle->dev, frame);
}
