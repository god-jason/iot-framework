#include "ch9121.h"
#include "log.h"

#define CH9121_TAG "ch9121"

static int ch9121_check(const ch9121_handle_t *handle)
{
    if (handle == NULL || !handle->initialized || handle->uart == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return DRIVER_OK;
}

int ch9121_init(ch9121_handle_t *handle, hal_uart_t *uart, uint32_t timeout_ms)
{
    if (handle == NULL || uart == NULL) {
        return (int)HAL_INVALID_PARAM;
    }

    handle->uart = uart;
    handle->timeout_ms = (timeout_ms != 0U) ? timeout_ms : CH9121_CMD_TIMEOUT_MS;
    handle->mode = CH9121_NET_TCP_SERVER;
    handle->initialized = true;

    LOG_I(CH9121_TAG, "init ok");
    return DRIVER_OK;
}

int ch9121_deinit(ch9121_handle_t *handle)
{
    if (handle == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    handle->uart = NULL;
    handle->initialized = false;
    return DRIVER_OK;
}

int ch9121_write(ch9121_handle_t *handle, const uint8_t *data, size_t len)
{
    int ret = ch9121_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (data == NULL || len == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    return (int)hal_uart_transmit(handle->uart, data, len, handle->timeout_ms);
}

int ch9121_read(ch9121_handle_t *handle, uint8_t *data, size_t len, uint32_t timeout_ms)
{
    int ret = ch9121_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (data == NULL || len == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    return (int)hal_uart_receive(handle->uart, data, len,
                                  (timeout_ms != 0U) ? timeout_ms : handle->timeout_ms);
}

int ch9121_send_command(ch9121_handle_t *handle, uint8_t cmd,
                        const uint8_t *data, size_t len)
{
    uint8_t frame[CH9121_FRAME_MAX];
    size_t n = 0U;
    size_t i;
    uint8_t sum = 0U;
    int ret = ch9121_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (len > CH9121_CMD_DATA_MAX || (data == NULL && len > 0U)) {
        return (int)HAL_INVALID_PARAM;
    }

    frame[n++] = CH9121_FRAME_HEAD0;
    frame[n++] = CH9121_FRAME_HEAD1;
    frame[n++] = cmd;
    frame[n++] = (uint8_t)len;
    for (i = 0U; i < len; i++) {
        frame[n++] = data[i];
    }
    /* 校验和为 CMD + LEN + DATA 的 8 位累加（不含帧头） */
    for (i = 2U; i < n; i++) {
        sum = (uint8_t)(sum + frame[i]);
    }
    frame[n++] = sum;

    return (int)hal_uart_transmit(handle->uart, frame, n, handle->timeout_ms);
}

int ch9121_read_response(ch9121_handle_t *handle, uint8_t *cmd, uint8_t *data,
                         size_t capacity, size_t *out_len)
{
    uint8_t head[4];
    uint8_t buf[CH9121_CMD_DATA_MAX + 1U];
    uint8_t sum = 0U;
    size_t i;
    int ret = ch9121_check(handle);

    if (ret != DRIVER_OK) {
        return ret;
    }
    if (cmd == NULL || data == NULL || out_len == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    *out_len = 0U;

    ret = (int)hal_uart_receive(handle->uart, head, sizeof(head),
                                 CH9121_RESPONSE_TIMEOUT_MS);
    if (ret != DRIVER_OK) {
        return ret;
    }
    if (head[0] != CH9121_FRAME_HEAD0 || head[1] != CH9121_FRAME_HEAD1) {
        return (int)HAL_IO_ERROR;
    }
    if (head[3] > capacity || head[3] > CH9121_CMD_DATA_MAX) {
        return (int)HAL_OVERFLOW;
    }

    ret = (int)hal_uart_receive(handle->uart, buf, (size_t)head[3] + 1U,
                                 CH9121_RESPONSE_TIMEOUT_MS);
    if (ret != DRIVER_OK) {
        return ret;
    }

    sum = (uint8_t)(head[2] + head[3]);
    for (i = 0U; i < head[3]; i++) {
        sum = (uint8_t)(sum + buf[i]);
    }
    if (sum != buf[head[3]]) {
        return (int)HAL_CRC_ERROR;
    }

    *cmd = head[2];
    for (i = 0U; i < head[3]; i++) {
        data[i] = buf[i];
    }
    *out_len = head[3];
    return DRIVER_OK;
}

int ch9121_set_ip(ch9121_handle_t *handle, const uint8_t ip[4])
{
    if (ip == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    /* CH9121 按字节顺序写入 IPv4 地址（192,168,1,200） */
    return ch9121_send_command(handle, CH9121_CMD_SET_IP, ip, 4U);
}

int ch9121_set_subnet(ch9121_handle_t *handle, const uint8_t mask[4])
{
    if (mask == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return ch9121_send_command(handle, CH9121_CMD_SET_SUBNET, mask, 4U);
}

int ch9121_set_gateway(ch9121_handle_t *handle, const uint8_t gateway[4])
{
    if (gateway == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return ch9121_send_command(handle, CH9121_CMD_SET_GATEWAY, gateway, 4U);
}

int ch9121_set_local_port(ch9121_handle_t *handle, uint16_t port)
{
    uint8_t buf[2];

    /* 端口按小端发送（需按 datasheet/固件确认） */
    buf[0] = (uint8_t)(port & 0xFFU);
    buf[1] = (uint8_t)((port >> 8) & 0xFFU);
    return ch9121_send_command(handle, CH9121_CMD_SET_LOCAL_PORT, buf, sizeof(buf));
}

int ch9121_set_dest_ip(ch9121_handle_t *handle, const uint8_t ip[4])
{
    if (ip == NULL) {
        return (int)HAL_INVALID_PARAM;
    }
    return ch9121_send_command(handle, CH9121_CMD_SET_DEST_IP, ip, 4U);
}

int ch9121_set_dest_port(ch9121_handle_t *handle, uint16_t port)
{
    uint8_t buf[2];

    buf[0] = (uint8_t)(port & 0xFFU);
    buf[1] = (uint8_t)((port >> 8) & 0xFFU);
    return ch9121_send_command(handle, CH9121_CMD_SET_DEST_PORT, buf, sizeof(buf));
}

int ch9121_set_mode(ch9121_handle_t *handle, ch9121_net_mode_t mode)
{
    uint8_t value;
    int ret;

    if (mode != CH9121_NET_TCP_SERVER && mode != CH9121_NET_TCP_CLIENT &&
        mode != CH9121_NET_UDP_SERVER && mode != CH9121_NET_UDP_CLIENT) {
        return (int)HAL_INVALID_PARAM;
    }

    switch (mode) {
    case CH9121_NET_TCP_CLIENT:
        value = CH9121_MODE_TCP_CLIENT;
        break;
    case CH9121_NET_UDP_SERVER:
        value = CH9121_MODE_UDP_SERVER;
        break;
    case CH9121_NET_UDP_CLIENT:
        value = CH9121_MODE_UDP_CLIENT;
        break;
    case CH9121_NET_TCP_SERVER:
    default:
        value = CH9121_MODE_TCP_SERVER;
        break;
    }

    ret = ch9121_send_command(handle, CH9121_CMD_SET_MODE, &value, 1U);
    if (ret == DRIVER_OK) {
        handle->mode = mode;
    }
    return ret;
}

int ch9121_set_baudrate(ch9121_handle_t *handle, uint32_t baudrate)
{
    uint8_t buf[4];

    if (baudrate == 0U) {
        return (int)HAL_INVALID_PARAM;
    }
    /* 波特率按 4 字节大端发送（需按 datasheet/固件确认） */
    buf[0] = (uint8_t)((baudrate >> 24) & 0xFFU);
    buf[1] = (uint8_t)((baudrate >> 16) & 0xFFU);
    buf[2] = (uint8_t)((baudrate >> 8) & 0xFFU);
    buf[3] = (uint8_t)(baudrate & 0xFFU);
    return ch9121_send_command(handle, CH9121_CMD_SET_BAUD, buf, sizeof(buf));
}

int ch9121_save_reset(ch9121_handle_t *handle)
{
    int ret = ch9121_send_command(handle, CH9121_CMD_SAVE_RESET, NULL, 0U);

    if (ret != DRIVER_OK) {
        return ret;
    }
    driver_delay_ms(CH9121_SAVE_RESET_MS);
    return DRIVER_OK;
}
