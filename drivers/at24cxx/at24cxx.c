#include "at24cxx.h"
#include "ros.h"
#include "common.h"

typedef struct {
    uint32_t capacity;
    uint16_t page_size;
} at24cxx_model_info_t;

static const at24cxx_model_info_t s_model_info[] = {
    { 128U,    8U   },   /* 01  */
    { 256U,    8U   },   /* 02  */
    { 512U,    16U  },   /* 04  */
    { 1024U,   16U  },   /* 08  */
    { 2048U,   16U  },   /* 16  */
    { 4096U,   32U  },   /* 32  */
    { 8192U,   32U  },   /* 64  */
    { 16384U,  64U  },   /* 128 */
    { 32768U,  64U  },   /* 256 */
    { 65536U,  128U }    /* 512 */
};

int at24cxx_init(at24cxx_handle_t *handle, hal_i2c_t *i2c,
                 uint16_t address, at24cxx_model_e model)
{
    if (handle == NULL || i2c == NULL || (uint32_t)model >= (sizeof(s_model_info)/sizeof((s_model_info)[0]))) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->i2c.handle = i2c;
    handle->i2c.address = (address != 0U) ? address : AT24CXX_DEFAULT_ADDRESS;
    handle->i2c.timeout_ms = HAL_I2C_TIMEOUT_DEFAULT;
    handle->model = model;
    handle->capacity = s_model_info[model].capacity;
    handle->page_size = s_model_info[model].page_size;
    handle->write_delay_ms = 5U;
    handle->initialized = false;

    if (hal_i2c_dev_probe(&handle->i2c) != HAL_OK) {
        LOG_E("at24cxx", "probe failed at 0x%02X", handle->i2c.address);
        return (int)HAL_NOT_FOUND;
    }

    handle->initialized = true;
    LOG_I("at24cxx", "init ok, capacity=%u B, page=%u B",
          (unsigned)handle->capacity, (unsigned)handle->page_size);
    return HAL_OK;
}

int at24cxx_deinit(at24cxx_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->initialized = false;
    handle->i2c.handle = NULL;
    return HAL_OK;
}

int at24cxx_is_ready(at24cxx_handle_t *handle)
{
    if (handle == NULL || handle->i2c.handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return (hal_i2c_dev_probe(&handle->i2c) == HAL_OK) ? 1 : 0;
}

static int at24cxx_wait_ready(at24cxx_handle_t *handle)
{
    uint32_t start = hal_tick_get();

    for (;;) {
        if (at24cxx_is_ready(handle) == 1) {
            return HAL_OK;
        }
        if ((uint32_t)(hal_tick_get() - start) > handle->write_delay_ms * 4U + 20U) {
            return (int)HAL_TIMEOUT;
        }
        hal_delay_ms(1U);
    }
}

int at24cxx_read(at24cxx_handle_t *handle, uint32_t address, uint8_t *data, size_t len)
{
    if (handle == NULL || data == NULL || len == 0U || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (address >= handle->capacity || len > handle->capacity - address) {
        return (int)HAL_OVERFLOW;
    }
    return hal_i2c_dev_read_reg16(&handle->i2c, (uint16_t)address, data, len);
}

int at24cxx_write(at24cxx_handle_t *handle, uint32_t address,
                  const uint8_t *data, size_t len)
{
    size_t written = 0U;

    if (handle == NULL || data == NULL || len == 0U || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (address >= handle->capacity || len > handle->capacity - address) {
        return (int)HAL_OVERFLOW;
    }

    while (written < len) {
        uint32_t current = address + (uint32_t)written;
        uint32_t page_room = handle->page_size - (current % handle->page_size);
        size_t chunk = len - written;
        int ret;

        if (chunk > page_room) {
            chunk = page_room;
        }

        ret = hal_i2c_dev_write_reg16(&handle->i2c, (uint16_t)current, &data[written], chunk);
        if (ret != HAL_OK) {
            return ret;
        }

        if (at24cxx_wait_ready(handle) != HAL_OK) {
            return (int)HAL_TIMEOUT;
        }
        written += chunk;
    }
    return HAL_OK;
}
