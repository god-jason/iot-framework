#include "wt5888e02.h"
#include "log.h"

#define WT5888E02_TAG "wt5888e02"

static int wt5888e02_check(const wt5888e02_handle_t *handle)
{
    if (handle == NULL || !handle->initialized) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->bus == WT5888E02_BUS_UART && handle->uart == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->bus == WT5888E02_BUS_ONELINE && handle->data_pin == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    return DRIVER_OK;
}

/* 组装 UART 帧：7E LEN CMD DATA... SUM EF */
static int wt5888e02_send_frame(wt5888e02_handle_t *handle, uint8_t cmd,
                                const uint8_t *data, size_t len)
{
    uint8_t frame[WT5888E02_FRAME_MAX];
    size_t n = 0U;
    size_t i;
    uint8_t sum;
    int ret = wt5888e02_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (len > WT5888E02_CMD_DATA_MAX || (data == NULL && len > 0U)) {
        return (int)HAL_INVALID_PARAM;
    }

    frame[n++] = WT5888E02_FRAME_HEAD;
    frame[n++] = (uint8_t)(len + 1U);   /* LEN = CMD + DATA */
    frame[n++] = cmd;
    for (i = 0U; i < len; i++) {
        frame[n++] = data[i];
    }
    sum = 0U;
    for (i = 1U; i < n; i++) {
        sum = (uint8_t)(sum + frame[i]);
    }
    frame[n++] = sum;
    frame[n++] = WT5888E02_FRAME_TAIL;

    if (handle->bus == WT5888E02_BUS_UART) {
        return (int)hal_uart_transmit(handle->uart, frame, n, handle->timeout_ms);
    }

    /* 一线串口固定 3 字节：cmd + 2 data（不足补 0） */
    {
        uint8_t payload[3];

        payload[0] = cmd;
        payload[1] = (len > 0U) ? data[0] : 0x00U;
        payload[2] = (len > 1U) ? data[1] : 0x00U;
        return wt5888e02_write(handle, payload, sizeof(payload));
    }
}

/* 一线串口：按脉宽编码发送 3 字节（高位在前） */
int wt5888e02_write(wt5888e02_handle_t *handle, const uint8_t *frame, size_t len)
{
    size_t i;
    uint8_t bit;
    int ret = wt5888e02_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (frame == NULL || len == 0U) {
        return (int)HAL_INVALID_PARAM;
    }

    if (handle->bus == WT5888E02_BUS_UART) {
        return (int)hal_uart_transmit(handle->uart, frame, len, handle->timeout_ms);
    }

    /* 启动脉冲 */
    if (driver_gpio_write(handle->data_pin, HAL_GPIO_LOW) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_us(WT5888E02_ONELINE_LATCH_US);
    if (driver_gpio_write(handle->data_pin, HAL_GPIO_HIGH) != DRIVER_OK) {
        return (int)HAL_IO_ERROR;
    }
    driver_delay_us(WT5888E02_ONELINE_BIT_GAP_US);

    for (i = 0U; i < len; i++) {
        for (bit = 0U; bit < 8U; bit++) {
            uint8_t value = (uint8_t)((frame[i] >> (7U - bit)) & 0x01U);

            (void)driver_gpio_write(handle->data_pin, HAL_GPIO_LOW);
            driver_delay_us((value != 0U) ? WT5888E02_ONELINE_BIT1_LOW_US
                                          : WT5888E02_ONELINE_BIT0_LOW_US);
            (void)driver_gpio_write(handle->data_pin, HAL_GPIO_HIGH);
            driver_delay_us(WT5888E02_ONELINE_BIT_GAP_US);
        }
    }
    return DRIVER_OK;
}

int wt5888e02_init(wt5888e02_handle_t *handle, hal_uart_t *uart,
                   hal_pin_t busy_pin)
{
    if (handle == NULL || uart == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->uart = uart;
    handle->data_pin = 0U;
    handle->busy_pin = busy_pin;
    handle->bus = WT5888E02_BUS_UART;
    handle->timeout_ms = WT5888E02_CMD_TIMEOUT_MS;
    handle->initialized = false;

    if (busy_pin != 0U) {
        if (driver_gpio_input(busy_pin, HAL_GPIO_PULLUP) != DRIVER_OK) {
            LOG_E(WT5888E02_TAG, "busy gpio init failed");
            return (int)HAL_IO_ERROR;
        }
    }

    handle->initialized = true;
    driver_delay_ms(WT5888E02_START_DELAY_MS);
    LOG_I(WT5888E02_TAG, "init ok (uart)");
    return DRIVER_OK;
}

int wt5888e02_init_oneline(wt5888e02_handle_t *handle, hal_pin_t data_pin,
                           hal_pin_t busy_pin)
{
    if (handle == NULL || data_pin == 0U) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->uart = NULL;
    handle->data_pin = data_pin;
    handle->busy_pin = busy_pin;
    handle->bus = WT5888E02_BUS_ONELINE;
    handle->timeout_ms = WT5888E02_CMD_TIMEOUT_MS;
    handle->initialized = false;

    if (driver_gpio_output(data_pin, HAL_GPIO_HIGH) != DRIVER_OK) {
        LOG_E(WT5888E02_TAG, "data gpio init failed");
        return (int)HAL_IO_ERROR;
    }
    if (busy_pin != 0U) {
        if (driver_gpio_input(busy_pin, HAL_GPIO_PULLUP) != DRIVER_OK) {
            LOG_E(WT5888E02_TAG, "busy gpio init failed");
            return (int)HAL_IO_ERROR;
        }
    }

    handle->initialized = true;
    driver_delay_ms(WT5888E02_START_DELAY_MS);
    LOG_I(WT5888E02_TAG, "init ok (one-line)");
    return DRIVER_OK;
}

int wt5888e02_deinit(wt5888e02_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->data_pin != 0U) {
        (void)driver_gpio_write(handle->data_pin, HAL_GPIO_HIGH);
    }
    handle->uart = NULL;
    handle->initialized = false;
    return DRIVER_OK;
}

int wt5888e02_play(wt5888e02_handle_t *handle, uint16_t index)
{
    uint8_t data[2];

    data[0] = (uint8_t)(index >> 8);
    data[1] = (uint8_t)(index & 0xFFU);
    return wt5888e02_send_frame(handle, WT5888E02_CMD_PLAY, data, sizeof(data));
}

int wt5888e02_stop(wt5888e02_handle_t *handle)
{
    return wt5888e02_send_frame(handle, WT5888E02_CMD_STOP, NULL, 0U);
}

int wt5888e02_set_volume(wt5888e02_handle_t *handle, uint8_t volume)
{
    if (volume > WT5888E02_VOLUME_MAX) {
        return (int)HAL_INVALID_PARAM;
    }
    return wt5888e02_send_frame(handle, WT5888E02_CMD_SET_VOLUME, &volume, 1U);
}

int wt5888e02_set_loop(wt5888e02_handle_t *handle, bool loop)
{
    uint8_t value = loop ? 0x01U : 0x00U;

    return wt5888e02_send_frame(handle, WT5888E02_CMD_SET_LOOP, &value, 1U);
}

int wt5888e02_busy(wt5888e02_handle_t *handle, bool *busy)
{
    int level;
    int ret = wt5888e02_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (busy == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    if (handle->busy_pin == 0U) {
        return (int)HAL_NOT_SUPPORTED;
    }

    level = driver_gpio_read(handle->busy_pin);
    if (level < 0) {
        return (int)HAL_IO_ERROR;
    }

#if WT5888E02_BUSY_ACTIVE_LOW
    *busy = (level == 0);
#else
    *busy = (level != 0);
#endif
    return DRIVER_OK;
}
