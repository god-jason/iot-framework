#include "ld3320.h"
#include "log.h"

#define LD3320_TAG "ld3320"

static int ld3320_check(const ld3320_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int ld3320_write(ld3320_handle_t *handle, uint8_t reg, uint8_t value)
{
    uint8_t frame[3];
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }

    frame[0] = LD3320_SPI_CMD_WRITE;
    frame[1] = reg;
    frame[2] = value;

    driver_spi_select(&handle->spi);
    ret = driver_spi_write(&handle->spi, frame, sizeof(frame));
    driver_spi_deselect(&handle->spi);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_us(10U);
    return DRIVER_OK;
}

int ld3320_read(ld3320_handle_t *handle, uint8_t reg, uint8_t *value)
{
    uint8_t frame[2];
    uint8_t rx = 0U;
    int ret;

    if (handle == NULL || !handle->initialized) {
        return (int)PORT_INVALID_PARAM;
    }
    if (value == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    frame[0] = LD3320_SPI_CMD_READ;
    frame[1] = reg;

    driver_spi_select(&handle->spi);
    ret = driver_spi_write(&handle->spi, frame, sizeof(frame));
    if (ret == DRIVER_OK) {
        ret = driver_spi_read(&handle->spi, &rx, 1U);
    }
    driver_spi_deselect(&handle->spi);
    if (ret != DRIVER_OK) {
        return ret;
    }

    *value = rx;
    return DRIVER_OK;
}

int ld3320_write_regs(ld3320_handle_t *handle, const uint8_t *pairs, size_t count)
{
    size_t i;
    int ret;

    if (pairs == NULL || count == 0U) {
        return (int)PORT_INVALID_PARAM;
    }
    ret = ld3320_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    for (i = 0U; i < count; i++) {
        ret = ld3320_write(handle, pairs[i * 2U], pairs[(i * 2U) + 1U]);
        if (ret != DRIVER_OK) {
            return ret;
        }
    }
    return DRIVER_OK;
}

int ld3320_init(ld3320_handle_t *handle, port_spi_handle_t *spi, port_pin_t cs,
                port_pin_t reset_pin)
{
    uint8_t id = 0U;
    int ret;

    if (handle == NULL || spi == NULL) {
        return (int)PORT_INVALID_PARAM;
    }

    handle->spi.handle = spi;
    handle->spi.cs = cs;
    handle->spi.timeout_ms = DRIVER_SPI_TIMEOUT_DEFAULT;
    handle->reset_pin = reset_pin;
    handle->irq_pin = 0U;
    handle->initialized = false;

    if (cs != 0U) {
        if (driver_gpio_output(cs, PORT_GPIO_HIGH) != DRIVER_OK) {
            LOG_E(LD3320_TAG, "cs gpio init failed");
            return (int)PORT_IO_ERROR;
        }
    }

    if (reset_pin != 0U) {
        if (driver_gpio_output(reset_pin, PORT_GPIO_LOW) != DRIVER_OK) {
            LOG_E(LD3320_TAG, "reset gpio init failed");
            return (int)PORT_IO_ERROR;
        }
        driver_delay_ms(LD3320_RESET_LOW_MS);
        if (driver_gpio_write(reset_pin, PORT_GPIO_HIGH) != DRIVER_OK) {
            return (int)PORT_IO_ERROR;
        }
        driver_delay_ms(LD3320_RESET_WAIT_MS);
    }

    handle->initialized = true;

    ret = ld3320_read(handle, LD3320_REG_CHIP_ID, &id);
    if (ret != DRIVER_OK) {
        LOG_W(LD3320_TAG, "chip id read failed");
    } else {
        LOG_I(LD3320_TAG, "init ok, chip id 0x%02X", id);
    }

    /* 进入空闲状态；DSP 固件与关键词表需按 datasheet/官方固件补充 */
    (void)ld3320_write(handle, LD3320_REG_CMD, LD3320_CMD_RESET);
    driver_delay_ms(LD3320_START_WAIT_MS);
    return DRIVER_OK;
}

int ld3320_deinit(ld3320_handle_t *handle)
{
    if (handle == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    if (handle->initialized) {
        (void)ld3320_stop(handle);
        if (handle->reset_pin != 0U) {
            (void)driver_gpio_write(handle->reset_pin, PORT_GPIO_LOW);
        }
    }
    handle->initialized = false;
    return DRIVER_OK;
}

int ld3320_start(ld3320_handle_t *handle)
{
    int ret = ld3320_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }

    /* 需按 datasheet/固件补充：写 DSP 固件 -> 配置麦克风 -> 写关键词表 */
    ret = ld3320_write(handle, LD3320_REG_ASR_FLAG, 0x00U);
    if (ret != DRIVER_OK) {
        return ret;
    }
    ret = ld3320_write(handle, LD3320_REG_CMD, LD3320_CMD_ASR_START);
    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(LD3320_START_WAIT_MS);
    return DRIVER_OK;
}

int ld3320_stop(ld3320_handle_t *handle)
{
    int ret = ld3320_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    return ld3320_write(handle, LD3320_REG_CMD, LD3320_CMD_ASR_STOP);
}

int ld3320_get_result(ld3320_handle_t *handle, uint8_t *index)
{
    uint32_t start;
    uint8_t flag = 0U;
    uint8_t value = 0U;
    int ret;

    if (index == NULL) {
        return (int)PORT_INVALID_PARAM;
    }
    *index = 0U;
    ret = ld3320_check(handle);
    if (ret != DRIVER_OK) {
        return ret;
    }

    start = driver_tick_ms();
    for (;;) {
        ret = ld3320_read(handle, LD3320_REG_ASR_FLAG, &flag);
        if (ret != DRIVER_OK) {
            return ret;
        }
        if ((flag & LD3320_ASR_FLAG_DONE) != 0U) {
            break;
        }
        if ((uint32_t)(driver_tick_ms() - start) > LD3320_RESULT_TIMEOUT_MS) {
            return DRIVER_OK; /* 超时表示本帧无关键词，返回 0 */
        }
        driver_delay_ms(LD3320_POLL_MS);
    }

    ret = ld3320_read(handle, LD3320_REG_ASR_RESULT, &value);
    if (ret != DRIVER_OK) {
        return ret;
    }
    *index = value;
    /* 清除完成标志，等待下一次识别 */
    (void)ld3320_write(handle, LD3320_REG_ASR_FLAG, 0x00U);
    return DRIVER_OK;
}

int ld3320_play_start(ld3320_handle_t *handle)
{
    int ret = ld3320_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    /* 需按 datasheet/固件补充：DAC 使能、音量、播放源选择 */
    return ld3320_write(handle, LD3320_REG_CMD, LD3320_CMD_PLAY_START);
}

int ld3320_play_stop(ld3320_handle_t *handle)
{
    int ret = ld3320_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    return ld3320_write(handle, LD3320_REG_CMD, LD3320_CMD_PLAY_STOP);
}
