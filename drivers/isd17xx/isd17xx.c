#include "isd17xx.h"
#include "log.h"

#define ISD17XX_TAG "isd17xx"

static int isd17xx_check(const isd17xx_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    return DRIVER_OK;
}

/* 一次完整 SPI 事务：CS 拉低 -> 发送 -> 接收 -> CS 拉高 */
static int isd17xx_transfer(isd17xx_handle_t *handle, const uint8_t *tx,
                            uint8_t *rx, size_t len)
{
    int ret;

    driver_spi_select(&handle->spi);
    ret = driver_spi_transfer(&handle->spi, tx, rx, len);
    driver_spi_deselect(&handle->spi);
    return ret;
}

int isd17xx_write(isd17xx_handle_t *handle, uint8_t cmd, uint16_t address)
{
    uint8_t tx[3];
    size_t len;

    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }

    tx[0] = cmd;
    tx[1] = (uint8_t)((address >> 8) & 0xFFU);
    tx[2] = (uint8_t)(address & 0xFFU);
    /* 单字节命令只需 1 字节；三字节命令带地址 */
    len = ((cmd & 0xF0U) == ISD17XX_CMD_PLAY_ADDR ||
           (cmd & 0xF0U) == ISD17XX_CMD_REC_ADDR ||
           (cmd & 0xF0U) == ISD17XX_CMD_ERASE_ADDR ||
           (cmd & 0xF0U) == ISD17XX_CMD_G_ERASE_ADDR) ? 3U : 1U;

    return isd17xx_transfer(handle, tx, NULL, len);
}

int isd17xx_read(isd17xx_handle_t *handle, uint16_t *status)
{
    uint8_t tx[3];
    uint8_t rx[3];
    int ret;

    if (status == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = isd17xx_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    tx[0] = ISD17XX_CMD_RD_STATUS;
    tx[1] = 0x00U;
    tx[2] = 0x00U;
    ret = isd17xx_transfer(handle, tx, rx, sizeof(tx));
    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 命令字节期间返回的状态为上一状态，取随后 2 字节 */
    *status = (uint16_t)(((uint16_t)rx[1] << 8) | rx[2]);
    return DRIVER_OK;
}

int isd17xx_wait_ready(isd17xx_handle_t *handle, uint32_t timeout_ms)
{
    uint32_t start;
    uint16_t status = 0U;
    int ret = isd17xx_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }

    start = driver_tick_ms();
    for (;;) {
        ret = isd17xx_read(handle, &status);
        if (ret != DRIVER_OK) {
            return ret;
        }
        if ((status & ISD17XX_SR_READY) != 0U) {
            return DRIVER_OK;
        }
        if ((uint32_t)(driver_tick_ms() - start) > timeout_ms) {
            return (int)HAL_TIMEOUT;
        }
        driver_delay_ms(ISD17XX_POLL_MS);
    }
}

int isd17xx_is_ready(isd17xx_handle_t *handle, bool *ready)
{
    uint16_t status = 0U;
    int ret;

    if (ready == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    ret = isd17xx_read(handle, &status);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *ready = ((status & ISD17XX_SR_READY) != 0U);
    return DRIVER_OK;
}

int isd17xx_init(isd17xx_handle_t *handle, hal_spi_t *spi, hal_pin_t cs)
{
    uint8_t tx[3];
    uint8_t rx[3];
    int ret;

    if (handle == NULL || spi == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->spi.handle = spi;
    handle->spi.cs = cs;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->device_id = 0U;
    handle->powered = false;
    handle->initialized = false;

    if (cs != 0U) {
        if (driver_gpio_output(cs, HAL_GPIO_HIGH) != DRIVER_OK) {
            LOG_E(ISD17XX_TAG, "cs gpio init failed");
            return (int)HAL_IO_ERROR;
        }
    }

    handle->initialized = true;
    ret = isd17xx_power_up(handle);
    if (ret != DRIVER_OK) {
        handle->initialized = false;
        return ret;
    }

    /* 读取器件 ID（命令 0x0A，状态/ID 在后续字节返回） */
    tx[0] = ISD17XX_CMD_RD_DEVID;
    tx[1] = 0x00U;
    tx[2] = 0x00U;
    ret = isd17xx_transfer(handle, tx, rx, sizeof(tx));
    if (ret == DRIVER_OK) {
        handle->device_id = rx[2];
    } else {
        LOG_W(ISD17XX_TAG, "device id read failed");
    }

    LOG_I(ISD17XX_TAG, "init ok, devid 0x%02X", handle->device_id);
    return DRIVER_OK;
}

int isd17xx_deinit(isd17xx_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)isd17xx_stop(handle);
        (void)isd17xx_power_down(handle);
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int isd17xx_power_up(isd17xx_handle_t *handle)
{
    uint8_t cmd = ISD17XX_CMD_POWER_UP;
    int ret;

    if (handle == NULL || handle->spi.handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = isd17xx_transfer(handle, &cmd, NULL, 1U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(ISD17XX_POWER_UP_MS);
    handle->powered = true;
    return DRIVER_OK;
}

int isd17xx_power_down(isd17xx_handle_t *handle)
{
    uint8_t cmd = ISD17XX_CMD_POWER_DOWN;
    int ret;

    if (handle == NULL || handle->spi.handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = isd17xx_transfer(handle, &cmd, NULL, 1U);
    driver_delay_ms(ISD17XX_POWER_DOWN_MS);
    if (ret == DRIVER_OK) {
        handle->powered = false;
    }
    return ret;
}

int isd17xx_play(isd17xx_handle_t *handle, uint16_t address)
{
    int ret = isd17xx_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (address > ISD17XX_MAX_ADDRESS) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = isd17xx_write(handle, ISD17XX_CMD_PLAY_ADDR, address);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(ISD17XX_CMD_READY_MS);
    return DRIVER_OK;
}

int isd17xx_record(isd17xx_handle_t *handle, uint16_t address)
{
    int ret = isd17xx_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (address > ISD17XX_MAX_ADDRESS) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = isd17xx_write(handle, ISD17XX_CMD_REC_ADDR, address);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(ISD17XX_CMD_READY_MS);
    return DRIVER_OK;
}

int isd17xx_erase(isd17xx_handle_t *handle, uint16_t address)
{
    int ret = isd17xx_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (address > ISD17XX_MAX_ADDRESS) {
        return (int)HAL_INVALID_PARAM;
    }

    ret = isd17xx_write(handle, ISD17XX_CMD_ERASE_ADDR, address);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(ISD17XX_CMD_READY_MS);
    return isd17xx_wait_ready(handle, ISD17XX_ERASE_TIMEOUT_MS);
}

int isd17xx_erase_all(isd17xx_handle_t *handle)
{
    int ret = isd17xx_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = isd17xx_write(handle, ISD17XX_CMD_G_ERASE_ADDR, 0x0000U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    return isd17xx_wait_ready(handle, ISD17XX_ERASE_TIMEOUT_MS);
}

int isd17xx_stop(isd17xx_handle_t *handle)
{
    int ret = isd17xx_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    return isd17xx_write(handle, ISD17XX_CMD_STOP, 0x0000U);
}
