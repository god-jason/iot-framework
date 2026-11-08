#include "w25qxx.h"
#include "w25qxx_register.h"
#include "log.h"

static int w25qxx_send_cmd_addr(w25qxx_handle_t *handle, uint8_t cmd, uint32_t address)
{
    uint8_t buf[4];

    buf[0] = cmd;
    buf[1] = (uint8_t)((address >> 16) & 0xFFU);
    buf[2] = (uint8_t)((address >> 8) & 0xFFU);
    buf[3] = (uint8_t)(address & 0xFFU);
    return driver_spi_write(&handle->spi, buf, sizeof(buf));
}

static int w25qxx_write_enable(w25qxx_handle_t *handle)
{
    uint8_t cmd = W25QXX_CMD_WRITE_ENABLE;

    return driver_spi_write_cs(&handle->spi, &cmd, 1U);
}

static int w25qxx_read_status(w25qxx_handle_t *handle, uint8_t *status)
{
    uint8_t cmd = W25QXX_CMD_READ_STATUS1;

    if (status == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    driver_spi_select(&handle->spi);
    if (driver_spi_write(&handle->spi, &cmd, 1U) != DRIVER_OK ||
        driver_spi_read(&handle->spi, status, 1U) != DRIVER_OK) {
        driver_spi_deselect(&handle->spi);
        return (int)HAL_IO_ERROR;
    }
    driver_spi_deselect(&handle->spi);
    return DRIVER_OK;
}

int w25qxx_wait_busy(w25qxx_handle_t *handle, uint32_t timeout_ms)
{
    uint32_t start;
    uint8_t status = 0U;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    start = driver_tick_ms();
    for (;;) {
        if (w25qxx_read_status(handle, &status) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
        if ((status & W25QXX_STATUS_BUSY) == 0U) {
            return DRIVER_OK;
        }
        if ((uint32_t)(driver_tick_ms() - start) > timeout_ms) {
            LOG_E("w25qxx", "busy timeout");
            return (int)HAL_TIMEOUT;
        }
        driver_delay_ms(1U);
    }
}

int w25qxx_read_id(w25qxx_handle_t *handle, w25qxx_id_t *id)
{
    uint8_t cmd = W25QXX_CMD_READ_ID;
    uint8_t buf[3];

    if (handle == NULL || id == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    driver_spi_select(&handle->spi);
    if (driver_spi_write(&handle->spi, &cmd, 1U) != DRIVER_OK ||
        driver_spi_read(&handle->spi, buf, sizeof(buf)) != DRIVER_OK) {
        driver_spi_deselect(&handle->spi);
        return (int)HAL_IO_ERROR;
    }
    driver_spi_deselect(&handle->spi);

    id->manufacturer = buf[0];
    id->memory_type = buf[1];
    id->capacity_id = buf[2];
    return DRIVER_OK;
}

int w25qxx_init(w25qxx_handle_t *handle, hal_spi_t *spi, hal_pin_t cs)
{
    uint8_t status = 0U;
    uint8_t cmd = W25QXX_CMD_RELEASE_PD;

    if (handle == NULL || spi == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->spi.handle = spi;
    handle->spi.cs = cs;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->capacity = 0U;
    handle->initialized = false;

    if (cs != 0U) {
        if (driver_gpio_output(cs, HAL_GPIO_HIGH) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }
    }

    (void)driver_spi_write_cs(&handle->spi, &cmd, 1U);
    driver_delay_ms(1U);

    if (w25qxx_read_id(handle, &handle->id) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (handle->id.manufacturer != 0xEFU) {
        LOG_E("w25qxx", "unknown manufacturer 0x%02X", handle->id.manufacturer);
        return (int)HAL_NOT_FOUND;
    }

    /* capacity_id 为 2 的幂次（字节） */
    handle->capacity = (handle->id.capacity_id < 32U)
                           ? (1UL << handle->id.capacity_id)
                           : 0U;

    handle->initialized = true;

    (void)w25qxx_read_status(handle, &status);
    LOG_I("w25qxx", "init ok: %02X %02X %02X, %u KB",
          handle->id.manufacturer, handle->id.memory_type, handle->id.capacity_id,
          (unsigned)(handle->capacity / 1024U));
    return DRIVER_OK;
}

int w25qxx_deinit(w25qxx_handle_t *handle)
{
    uint8_t cmd = W25QXX_CMD_POWER_DOWN;

    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->spi.handle != NULL) {
        (void)driver_spi_write_cs(&handle->spi, &cmd, 1U);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int w25qxx_read(w25qxx_handle_t *handle, uint32_t address, void *data, size_t len)
{
    if (handle == NULL || data == NULL || len == 0U || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (address >= handle->capacity || len > handle->capacity - address) {
        return (int)HAL_OVERFLOW;
    }

    driver_spi_select(&handle->spi);
    if (w25qxx_send_cmd_addr(handle, W25QXX_CMD_READ_DATA, address) != DRIVER_OK ||
        driver_spi_read(&handle->spi, (uint8_t *)data, len) != DRIVER_OK) {
        driver_spi_deselect(&handle->spi);
        return (int)HAL_IO_ERROR;
    }
    driver_spi_deselect(&handle->spi);
    return DRIVER_OK;
}

int w25qxx_write(w25qxx_handle_t *handle, uint32_t address, const void *data, size_t len)
{
    const uint8_t *src = (const uint8_t *)data;
    size_t written = 0U;

    if (handle == NULL || data == NULL || len == 0U || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (address >= handle->capacity || len > handle->capacity - address) {
        return (int)HAL_OVERFLOW;
    }

    while (written < len) {
        uint32_t current = address + (uint32_t)written;
        size_t page_room = W25QXX_PAGE_SIZE - (current % W25QXX_PAGE_SIZE);
        size_t chunk = len - written;
        int ret;

        if (chunk > page_room) {
            chunk = page_room;
        }

        if (w25qxx_write_enable(handle) != DRIVER_OK) {
            return (int)HAL_IO_ERROR;
        }

        driver_spi_select(&handle->spi);
        if (w25qxx_send_cmd_addr(handle, W25QXX_CMD_PAGE_PROGRAM, current) != DRIVER_OK ||
            driver_spi_write(&handle->spi, &src[written], chunk) != DRIVER_OK) {
            driver_spi_deselect(&handle->spi);
            return (int)HAL_IO_ERROR;
        }
        driver_spi_deselect(&handle->spi);

        ret = w25qxx_wait_busy(handle, W25QXX_PAGE_PROGRAM_MS);
        if (ret != DRIVER_OK) {
            return ret;
        }
        written += chunk;
    }
    return DRIVER_OK;
}

int w25qxx_erase_sector(w25qxx_handle_t *handle, uint32_t address)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (address >= handle->capacity) {
        return (int)HAL_OVERFLOW;
    }
    address &= ~(uint32_t)(W25QXX_SECTOR_SIZE - 1U);

    if (w25qxx_write_enable(handle) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    driver_spi_select(&handle->spi);
    if (w25qxx_send_cmd_addr(handle, W25QXX_CMD_SECTOR_ERASE, address) != DRIVER_OK) {
        driver_spi_deselect(&handle->spi);
        return (int)HAL_IO_ERROR;
    }
    driver_spi_deselect(&handle->spi);

    return w25qxx_wait_busy(handle, W25QXX_SECTOR_ERASE_MS);
}

int w25qxx_erase_block(w25qxx_handle_t *handle, uint32_t address)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (address >= handle->capacity) {
        return (int)HAL_OVERFLOW;
    }
    address &= ~(uint32_t)(W25QXX_BLOCK_SIZE - 1U);

    if (w25qxx_write_enable(handle) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }

    driver_spi_select(&handle->spi);
    if (w25qxx_send_cmd_addr(handle, W25QXX_CMD_BLOCK_ERASE_64K, address) != DRIVER_OK) {
        driver_spi_deselect(&handle->spi);
        return (int)HAL_IO_ERROR;
    }
    driver_spi_deselect(&handle->spi);

    return w25qxx_wait_busy(handle, W25QXX_BLOCK_ERASE_MS);
}

int w25qxx_erase_chip(w25qxx_handle_t *handle)
{
    uint8_t cmd = W25QXX_CMD_CHIP_ERASE;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (w25qxx_write_enable(handle) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    if (driver_spi_write_cs(&handle->spi, &cmd, 1U) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    return w25qxx_wait_busy(handle, W25QXX_CHIP_ERASE_MS);
}
