#include <string.h>
#include "as608.h"
#include "as608_register.h"
#include "log.h"

#define AS608_CHECK(h) \
    do { if ((h) == NULL || !(h)->initialized) { return (int)PORT_INVALID_PARAM; } } while (0)

int as608_write(as608_handle_t *h, uint8_t cmd, const uint8_t *params, size_t len)
{
    uint8_t buf[AS608_HEADER_SIZE + 1U + AS608_PARAM_MAX + AS608_CHECKSUM_SIZE];
    uint16_t plen;
    uint16_t sum = 0U;
    size_t i;

    AS608_CHECK(h);
    if (len > AS608_PARAM_MAX || (params == NULL && len > 0U)) {
        return (int)PORT_INVALID_PARAM;
    }
    buf[0] = AS608_HEAD0;
    buf[1] = AS608_HEAD1;
    buf[2] = (uint8_t)(h->address >> 24);
    buf[3] = (uint8_t)(h->address >> 16);
    buf[4] = (uint8_t)(h->address >> 8);
    buf[5] = (uint8_t)h->address;
    buf[6] = AS608_PACKAGE_ID;
    plen = (uint16_t)(len + 3U);   /* 指令 + 参数 + 校验和 */
    buf[7] = (uint8_t)(plen >> 8);
    buf[8] = (uint8_t)plen;
    buf[9] = cmd;
    if (len > 0U) { (void)memcpy(&buf[10], params, len); }
    for (i = 6U; i < 10U + len; i++) { sum = (uint16_t)(sum + buf[i]); }
    buf[10U + len] = (uint8_t)(sum >> 8);
    buf[11U + len] = (uint8_t)sum;
    if (port_uart_transmit(h->uart, buf, 12U + len, h->timeout_ms) != PORT_OK) {
        LOG_E("as608", "uart tx failed");
        return (int)PORT_IO_ERROR;
    }
    return DRIVER_OK;
}

int as608_read(as608_handle_t *h, as608_response_t *response)
{
    uint8_t head[AS608_HEADER_SIZE];
    uint8_t buf[AS608_RESPONSE_DATA_MAX + AS608_CHECKSUM_SIZE];
    uint16_t plen;
    uint16_t sum = 0U;
    uint16_t expected;
    size_t data_len;
    size_t i;

    AS608_CHECK(h);
    if (response == NULL) { return (int)PORT_INVALID_PARAM; }
    if (port_uart_receive(h->uart, head, sizeof(head), h->timeout_ms) != PORT_OK) {
        return (int)PORT_TIMEOUT;
    }
    if (head[0] != AS608_HEAD0 || head[1] != AS608_HEAD1) {
        LOG_E("as608", "bad header %02X %02X", head[0], head[1]);
        return (int)PORT_CRC_ERROR;
    }
    plen = (uint16_t)(((uint16_t)head[7] << 8) | head[8]);
    if (plen < 3U || plen > (uint16_t)sizeof(buf)) {
        return (int)PORT_OVERFLOW;
    }
    if (port_uart_receive(h->uart, buf, plen, h->timeout_ms) != PORT_OK) {
        return (int)PORT_TIMEOUT;
    }
    data_len = (size_t)plen - AS608_CHECKSUM_SIZE;
    if (data_len == 0U || data_len > sizeof(response->data) + 1U) {
        return (int)PORT_OVERFLOW;
    }
    for (i = 6U; i < AS608_HEADER_SIZE; i++) { sum = (uint16_t)(sum + head[i]); }
    for (i = 0U; i < data_len; i++) { sum = (uint16_t)(sum + buf[i]); }
    expected = (uint16_t)(((uint16_t)buf[data_len] << 8) | buf[data_len + 1U]);
    if (sum != expected) {
        LOG_E("as608", "checksum error %04X != %04X", sum, expected);
        return (int)PORT_CRC_ERROR;
    }
    response->confirm = buf[0];
    response->len = data_len - 1U;
    if (response->len > 0U) { (void)memcpy(response->data, &buf[1], response->len); }
    return DRIVER_OK;
}

int as608_verify_password(as608_handle_t *h)
{
    uint8_t p[4];
    as608_response_t resp;
    AS608_CHECK(h);
    p[0] = (uint8_t)(h->password >> 24);
    p[1] = (uint8_t)(h->password >> 16);
    p[2] = (uint8_t)(h->password >> 8);
    p[3] = (uint8_t)h->password;
    if (as608_write(h, AS608_CMD_VERIFY_PASSWORD, p, sizeof(p)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (as608_read(h, &resp) != DRIVER_OK) { return (int)PORT_TIMEOUT; }
    if (resp.confirm != AS608_CONFIRM_OK) {
        LOG_E("as608", "password rejected (0x%02X)", resp.confirm);
        return (int)PORT_PERMISSION;
    }
    return DRIVER_OK;
}

int as608_gen_image(as608_handle_t *h)
{
    as608_response_t resp;
    AS608_CHECK(h);
    if (as608_write(h, AS608_CMD_GET_IMAGE, NULL, 0U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (as608_read(h, &resp) != DRIVER_OK) { return (int)PORT_TIMEOUT; }
    if (resp.confirm == AS608_CONFIRM_NO_FINGER) { return (int)PORT_NOT_FOUND; }
    return (resp.confirm == AS608_CONFIRM_OK) ? DRIVER_OK : (int)PORT_ERROR;
}

int as608_image2tz(as608_handle_t *h, uint8_t buf_id)
{
    as608_response_t resp;
    AS608_CHECK(h);
    if (buf_id != 1U && buf_id != 2U) { return (int)PORT_INVALID_PARAM; }
    if (as608_write(h, AS608_CMD_IMAGE2TZ, &buf_id, 1U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (as608_read(h, &resp) != DRIVER_OK) { return (int)PORT_TIMEOUT; }
    return (resp.confirm == AS608_CONFIRM_OK) ? DRIVER_OK : (int)PORT_ERROR;
}

int as608_search(as608_handle_t *h, uint8_t buf_id, uint16_t *page, uint16_t *score)
{
    uint8_t p[5];
    as608_response_t resp;
    AS608_CHECK(h);
    if (buf_id != 1U && buf_id != 2U) { return (int)PORT_INVALID_PARAM; }
    p[0] = buf_id;
    p[1] = 0x00U; p[2] = 0x00U;   /* 起始页 */
    p[3] = 0xFFU; p[4] = 0xFFU;   /* 页数 65535 */
    if (as608_write(h, AS608_CMD_SEARCH, p, sizeof(p)) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (as608_read(h, &resp) != DRIVER_OK) { return (int)PORT_TIMEOUT; }
    if (resp.confirm == AS608_CONFIRM_NOT_FOUND) { return (int)PORT_NOT_FOUND; }
    if (resp.confirm != AS608_CONFIRM_OK || resp.len < 4U) { return (int)PORT_ERROR; }
    if (page != NULL) { *page = (uint16_t)(((uint16_t)resp.data[0] << 8) | resp.data[1]); }
    if (score != NULL) { *score = (uint16_t)(((uint16_t)resp.data[2] << 8) | resp.data[3]); }
    return DRIVER_OK;
}

int as608_get_count(as608_handle_t *h, uint16_t *count)
{
    as608_response_t resp;
    AS608_CHECK(h);
    if (count == NULL) { return (int)PORT_INVALID_PARAM; }
    if (as608_write(h, AS608_CMD_TEMPLATE_COUNT, NULL, 0U) != DRIVER_OK) {
        return (int)PORT_IO_ERROR;
    }
    if (as608_read(h, &resp) != DRIVER_OK) { return (int)PORT_TIMEOUT; }
    if (resp.confirm != AS608_CONFIRM_OK || resp.len < 2U) { return (int)PORT_ERROR; }
    *count = (uint16_t)(((uint16_t)resp.data[0] << 8) | resp.data[1]);
    return DRIVER_OK;
}

int as608_init(as608_handle_t *handle, port_uart_handle_t *uart, uint32_t password)
{
    if (handle == NULL || uart == NULL) { return (int)PORT_INVALID_PARAM; }
    (void)memset(handle, 0, sizeof(*handle));
    handle->uart = uart;
    handle->address = AS608_ADDRESS_DEFAULT;
    handle->password = (password != 0U) ? password : AS608_PASSWORD_DEFAULT;
    handle->timeout_ms = 1000U;
    handle->initialized = true;
    if (as608_verify_password(handle) != DRIVER_OK) {
        handle->initialized = false;
        LOG_E("as608", "init failed (verify password)");
        return (int)PORT_NOT_FOUND;
    }
    LOG_I("as608", "init ok");
    return DRIVER_OK;
}

int as608_deinit(as608_handle_t *handle)
{
    if (handle == NULL) { return (int)PORT_INVALID_PARAM; }
    handle->initialized = false;
    return DRIVER_OK;
}
