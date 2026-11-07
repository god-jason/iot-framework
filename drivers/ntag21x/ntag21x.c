#include <string.h>
#include "ntag21x.h"
#include "log.h"

#define NTAG21X_CHECK(h) \
    do { if ((h) == NULL || !(h)->initialized) { return (int)PORT_INVALID_PARAM; } } while (0)

int ntag21x_get_page_count(ntag21x_handle_t *h, uint16_t *pages)
{
    NTAG21X_CHECK(h);
    if (pages == NULL) { return (int)PORT_INVALID_PARAM; }
    *pages = h->page_count;
    return DRIVER_OK;
}

int ntag21x_set_page_count(ntag21x_handle_t *h, uint16_t pages)
{
    NTAG21X_CHECK(h);
    if (pages == 0U || pages > NTAG21X_PAGE_MAX) { return (int)PORT_INVALID_PARAM; }
    h->page_count = pages;
    return DRIVER_OK;
}

int ntag21x_read(ntag21x_handle_t *h, uint16_t page, uint8_t *data, size_t len)
{
    uint8_t buf[NTAG21X_PAGE_SIZE];
    size_t done = 0U;
    NTAG21X_CHECK(h);
    if (data == NULL || len == 0U) { return (int)PORT_INVALID_PARAM; }
    if (page >= h->page_count) { return (int)PORT_OVERFLOW; }
    if ((size_t)(h->page_count - page) * NTAG21X_PAGE_SIZE < len) { return (int)PORT_OVERFLOW; }
    while (done < len) {
        size_t chunk = len - done;
        if (chunk > NTAG21X_PAGE_SIZE) { chunk = NTAG21X_PAGE_SIZE; }
        if (driver_i2c_read_reg(&h->i2c, (uint8_t)page, buf, NTAG21X_PAGE_SIZE) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
        (void)memcpy(&data[done], buf, chunk);
        done += chunk;
        page++;
    }
    return DRIVER_OK;
}

int ntag21x_write(ntag21x_handle_t *h, uint16_t page, const uint8_t *data, size_t len)
{
    size_t done = 0U;
    NTAG21X_CHECK(h);
    if (data == NULL || len == 0U) { return (int)PORT_INVALID_PARAM; }
    if ((len % NTAG21X_PAGE_SIZE) != 0U) { return (int)PORT_INVALID_PARAM; }
    if (page >= h->page_count) { return (int)PORT_OVERFLOW; }
    if ((size_t)(h->page_count - page) * NTAG21X_PAGE_SIZE < len) { return (int)PORT_OVERFLOW; }
    while (done < len) {
        /* 每次写入完整一页；跨页时逐页下发 */
        if (driver_i2c_write_reg(&h->i2c, (uint8_t)page, &data[done],
                                 NTAG21X_PAGE_SIZE) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
        done += NTAG21X_PAGE_SIZE;
        page++;
    }
    return DRIVER_OK;
}

int ntag21x_init(ntag21x_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address)
{
    if (handle == NULL || i2c == NULL) { return (int)PORT_INVALID_PARAM; }
    (void)memset(handle, 0, sizeof(*handle));
    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : NTAG21X_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = DRIVER_I2C_TIMEOUT_DEFAULT;
    handle->page_size = NTAG21X_PAGE_SIZE;
    handle->page_count = 231U;
    if (driver_i2c_probe(&handle->i2c) != DRIVER_OK) {
        LOG_E("ntag21x", "probe failed at 0x%02X", handle->i2c.address);
        return (int)PORT_NOT_FOUND;
    }
    handle->initialized = true;
    return DRIVER_OK;
}

int ntag21x_deinit(ntag21x_handle_t *handle)
{
    if (handle == NULL) { return (int)PORT_INVALID_PARAM; }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return DRIVER_OK;
}
