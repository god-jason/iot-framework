#include "max31855.h"
#include "max31855_register.h"
#include "iot_logger.h"

/* 14 位有符号热电偶温度码 -> 摄氏度 */
static float max31855_decode_tc(uint32_t raw)
{
    int32_t code = (int32_t)(raw >> MAX31855_TC_SHIFT);

    /* 符号扩展 18 位到 32 位 */
    if ((code & 0x2000) != 0) {
        code |= (int32_t)0xFFFFC000;
    }
    return (float)code * MAX31855_TC_LSB_CELSIUS;
}

/* 12 位有符号内部温度码 -> 摄氏度 */
static float max31855_decode_internal(uint32_t raw)
{
    int32_t code = (int32_t)((raw >> MAX31855_INTERNAL_SHIFT) & MAX31855_INTERNAL_MASK);

    if ((code & 0x800) != 0) {
        code |= (int32_t)0xFFFFF000;
    }
    return (float)code * MAX31855_INTERNAL_LSB;
}

int max31855_init(max31855_handle_t *handle, port_spi_handle_t *spi, port_pin_t cs)
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

    /* 器件无 ID 寄存器，只能等待首次转换完成 */
    driver_delay_ms(MAX31855_CONV_DELAY_MS);

    handle->initialized = true;
    LOG_I("max31855", "init ok");
    return DRIVER_OK;
}

int max31855_deinit(max31855_handle_t *handle)
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

int max31855_read_raw(max31855_handle_t *handle, uint32_t *raw)
{
    uint8_t tx[MAX31855_FRAME_SIZE] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU };
    uint8_t rx[MAX31855_FRAME_SIZE] = { 0U, 0U, 0U, 0U };
    int ret;

    if (handle == NULL || raw == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, rx, MAX31855_FRAME_SIZE);
    driver_spi_deselect(&handle->spi);
    if (ret != DRIVER_OK) {
        LOG_E("max31855", "spi transfer failed: %d", ret);
        return (int)PORT_IO_ERROR;
    }

    *raw = ((uint32_t)rx[0] << 24) | ((uint32_t)rx[1] << 16) |
           ((uint32_t)rx[2] << 8) | (uint32_t)rx[3];
    return DRIVER_OK;
}

int max31855_read(max31855_handle_t *handle, float *celsius)
{
    uint32_t raw = 0U;
    int ret;

    if (celsius == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = max31855_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    if ((raw & MAX31855_FAULT_BIT) != 0U) {
        LOG_E("max31855", "fault: 0x%02X", (unsigned int)(raw & 0x07U));
        return (int)PORT_HW_ERROR;
    }

    *celsius = max31855_decode_tc(raw);
    return DRIVER_OK;
}

int max31855_read_internal(max31855_handle_t *handle, float *celsius)
{
    uint32_t raw = 0U;
    int ret;

    if (celsius == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = max31855_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *celsius = max31855_decode_internal(raw);
    return DRIVER_OK;
}

int max31855_get_fault(max31855_handle_t *handle, uint8_t *fault)
{
    uint32_t raw = 0U;
    int ret;

    if (fault == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    ret = max31855_read_raw(handle, &raw);
    if (ret != DRIVER_OK) {
        return ret;
    }

    *fault = (uint8_t)(raw & (MAX31855_FAULT_OC | MAX31855_FAULT_SCG | MAX31855_FAULT_SCV));
    return DRIVER_OK;
}
