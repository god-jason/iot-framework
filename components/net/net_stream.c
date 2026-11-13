/*
 * net_stream.c - 通用流式接口与 ros_io_t 适配
 *
 * 只依赖后端提供的原始接口（net_send / net_recv / net_close），
 * BSD socket 与 ML307 等后端共用本文件。
 */

#include "net.h"

#include <string.h>

int net_stream_init(net_stream_t *st, net_sock_t *sock, uint32_t timeout_ms)
{
    if (st == NULL || !net_is_open(sock)) {
        return NET_ERR_PARAM;
    }
    memset(st, 0, sizeof(*st));
    st->sock = sock;
    st->timeout_ms = (timeout_ms != 0U) ? timeout_ms : NET_DEFAULT_TIMEOUT;
    return NET_OK;
}

int net_stream_flush(net_stream_t *st)
{
    int rc;

    if (st == NULL || st->sock == NULL) {
        return NET_ERR_PARAM;
    }
    if (st->tx_len == 0U) {
        return NET_OK;
    }
    rc = net_send(st->sock, st->tx, st->tx_len);
    if (rc < 0) {
        return rc;
    }
    st->tx_len = 0U;
    return NET_OK;
}

int net_stream_write(net_stream_t *st, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t written = 0U;

    if (st == NULL || st->sock == NULL || (data == NULL && len != 0U)) {
        return NET_ERR_PARAM;
    }
    while (written < len) {
        uint32_t room = (uint32_t)NET_STREAM_TX_MAX - st->tx_len;
        uint32_t n;

        if (room == 0U) {
            int rc = net_stream_flush(st);

            if (rc < 0) {
                return rc;
            }
            room = NET_STREAM_TX_MAX;
        }
        n = len - written;
        if (n > room) {
            n = room;
        }
        memcpy(&st->tx[st->tx_len], &p[written], n);
        st->tx_len += n;
        written += n;
    }
    return (int)written;
}

int net_stream_read(net_stream_t *st, void *data, uint32_t len)
{
    uint32_t avail;
    uint32_t n;
    int rc;

    if (st == NULL || st->sock == NULL || data == NULL || len == 0U) {
        return NET_ERR_PARAM;
    }
    if (st->rx_pos < st->rx_len) {
        avail = st->rx_len - st->rx_pos;
        n = (len < avail) ? len : avail;
        memcpy(data, &st->rx[st->rx_pos], n);
        st->rx_pos += n;
        if (st->rx_pos >= st->rx_len) {
            st->rx_pos = 0U;
            st->rx_len = 0U;
        }
        return (int)n;
    }

    rc = net_recv(st->sock, st->rx, NET_STREAM_RX_MAX, st->timeout_ms);
    if (rc <= 0) {
        return rc;
    }
    st->rx_pos = 0U;
    st->rx_len = (uint32_t)rc;
    n = (len < st->rx_len) ? len : st->rx_len;
    memcpy(data, st->rx, n);
    st->rx_pos = n;
    if (st->rx_pos >= st->rx_len) {
        st->rx_pos = 0U;
        st->rx_len = 0U;
    }
    return (int)n;
}

int net_stream_close(net_stream_t *st)
{
    if (st == NULL) {
        return NET_ERR_PARAM;
    }
    (void)net_stream_flush(st);
    if (st->sock != NULL) {
        (void)net_close(st->sock);
    }
    st->rx_pos = 0U;
    st->rx_len = 0U;
    st->tx_len = 0U;
    return NET_OK;
}

/* ---------------- ros_io_t 适配 ---------------- */

static int net_io_read(ros_io_t *io, void *buf, uint32_t len)
{
    return net_stream_read((net_stream_t *)io->user, buf, len);
}

static int net_io_write(ros_io_t *io, const void *buf, uint32_t len)
{
    return net_stream_write((net_stream_t *)io->user, buf, len);
}

static int net_io_close(ros_io_t *io)
{
    return net_stream_close((net_stream_t *)io->user);
}

void net_io_bind(ros_io_t *io, net_stream_t *st)
{
    if (io == NULL) {
        return;
    }
    io->read = net_io_read;
    io->write = net_io_write;
    io->close = net_io_close;
    io->user = st;
}
