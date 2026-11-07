#include "max6675.h"
#include "max6675_register.h"
#include "log.h"

int max6675_init(max6675_handle_t *handle, port_spi_handle_t *spi, port_pin_t cs)
{
    if (handle == NULL || spi == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->spi.handle = spi;
    handle->spi.cs = cs;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->initialized = false;

    if (cs != 0U) {
        driver_spi_deselect(&handle->spi);
    }

    /* 器件无 ID 寄存器，无法探测；等待首次转换完成 */
    driver_delay_ms(MAX6675_CONV_DELAY_MS);

    handle->initialized = true;
    LOG_I("max6675", "init ok");
    return DRIVER_OK;
}

int max6675_deinit(max6675_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return (int)PORT_NOT_READY;
    }
    if (handle->spi.cs != 0U) {
        driver_spi_deselect(&handle->spi);
    }
    handle->initialized = false;
    handle->spi.handle = NULL;
    return DRIVER_OK;
}

int max6675_read_raw(max6675_handle_t *handle, uint16_t *raw)
{
    uint8_t tx[MAX6675_FRAME_SIZE] = { 0xFFU, 0xFFU };
    uint8_t rx[MAX6675_FRAME_SIZE] = { 0U, 0U };
    int ret;

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, rx, MAX6675_FRAME_SIZE);
    driver_spi_deselect(&handle->spi);
    if (ret != DRIVER_OK) {
        LOG_E("max6675", "spi transfer failed: %d", ret);
        return (int)PORT_IO_ERROR;
    }

    *raw = (uint16_t)(((uint16_t)rx[0] << 8) | (uint16_t)rx[1]);
    return DRIVER_OK;
}

int max6675_read(max6675_handle_t *handle, float *celsius)
{
    uint16_t raw = 0U;
    int ret;

    if (celsius == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = max6675_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* bit2 = 1 表示热电偶开路（未接或断线） */
    if ((raw & MAX6675_DATA_MASK) != 0U) {
        LOG_E("max6675", "thermocouple open");
        return (int)PORT_HW_ERROR;
    }

    *celsius = (float)(raw >> MAX6675_TEMP_SHIFT) * MAX6675_LSB_CELSIUS;
    return DRIVER_OK;
}
