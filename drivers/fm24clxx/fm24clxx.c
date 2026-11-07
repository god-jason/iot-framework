#include <string.h>
#include "fm24clxx.h"
#include "log.h"

#define FM24CLXX_CHECK(h) \
    do { if ((h) == NULL || !(h)->initialized) { return (int)PORT_INVALID_PARAM; } } while (0)

int fm24clxx_get_capacity(fm24clxx_handle_t *h, uint32_t *capacity)
{
    FM24CLXX_CHECK(h);
    if (capacity == NULL) { return (int)PORT_INVALID_PARAM; }
    *capacity = h->capacity;
    return DRIVER_OK;
}

int fm24clxx_is_ready(fm24clxx_handle_t *h)
{
    if (h == NULL || h->i2c.handle == NULL) { return (int)PORT_INVALID_PARAM; }
    return (driver_i2c_probe(&h->i2c) == DRIVER_OK) ? 1 : 0;
}

int fm24clxx_read(fm24clxx_handle_t *h, uint16_t address, uint8_t *data, size_t len)
{
    size_t done = 0U;
    FM24CLXX_CHECK(h);
    if (data == NULL || len == 0U) { return (int)PORT_INVALID_PARAM; }
    if ((uint32_t)address >= h->capacity || len > h->capacity - address) {
        return (int)PORT_OVERFLOW;
    }
    while (done < len) {
        uint16_t current = (uint16_t)(address + done);
        size_t chunk = len - done;
        if (chunk > FM24CLXX_CHUNK) { chunk = FM24CLXX_CHUNK; }
        if (driver_i2c_read_reg16(&h->i2c, current, &data[done], chunk) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
        done += chunk;
    }
    return DRIVER_OK;
}

int fm24clxx_write(fm24clxx_handle_t *h, uint16_t address, const uint8_t *data, size_t len)
{
    size_t done = 0U;
    FM24CLXX_CHECK(h);
    if (data == NULL || len == 0U) { return (int)PORT_INVALID_PARAM; }
    if ((uint32_t)address >= h->capacity || len > h->capacity - address) {
        return (int)PORT_OVERFLOW;
    }
    while (done < len) {
        uint16_t current = (uint16_t)(address + done);
        size_t chunk = len - done;
        if (chunk > FM24CLXX_CHUNK) { chunk = FM24CLXX_CHUNK; }
        /* FRAM 写入立即生效，无需轮询写周期 */
        if (driver_i2c_write_reg16(&h->i2c, current, &data[done], chunk) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
        done += chunk;
    }
    return DRIVER_OK;
}

int fm24clxx_init(fm24clxx_handle_t *handle, port_i2c_handle_t *i2c,
                  uint16_t address, uint32_t capacity)
{
    if (handle == NULL || i2c == NULL) { return (int)PORT_INVALID_PARAM; }
    if (capacity < FM24CLXX_CAPACITY_MIN || capacity > FM24CLXX_CAPACITY_MAX) {
        return (int)PORT_INVALID_PARAM;
    }
    (void)memset(handle, 0, sizeof(*handle));
    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : FM24CLXX_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->capacity = capacity;
    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("fm24clxx", "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }
    handle->initialized = true;
    LOG_I("fm24clxx", "init ok, %u B", (unsigned)capacity);
    return DRIVER_OK;
}

int fm24clxx_deinit(fm24clxx_handle_t *handle)
{
    if (handle == NULL) { return (int)PORT_INVALID_PARAM; }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}
