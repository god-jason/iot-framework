/*
 * PC 平台 UART 模拟实现。
 *
 * 实例映射：
 *   - UART1：handle->base.instance == (void *)1，
 *     发送写 stdout，接收非阻塞读 stdin（标准控制台）；
 *   - UART2..N：handle->base.instance == (void *)2..(PC_UART_MAX+1)，
 *     使用内存缓冲区模拟串口；
 *   - instance 为 NULL：自动分配一个空闲的 UART2..N。
 *
 * 测试注入接口 ros_pc_uart_inject()/ros_pc_uart_get_tx() 对应虚拟串口。
 */

#include "pc.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <conio.h>
#else
#include <unistd.h>
#include <sys/select.h>
#endif

#define PC_UART_MAX        8
#define PC_UART_BUF_SIZE   4096U

typedef struct {
    bool used;
    bool stdio;                 /* true: UART1，映射到 stdin/stdout */
    const void *owner;
    uint8_t tx[PC_UART_BUF_SIZE];
    size_t tx_len;
    uint8_t rx[PC_UART_BUF_SIZE];
    size_t rx_len;
} pc_uart_ctx_t;

static pc_uart_ctx_t s_uart[PC_UART_MAX];   /* UART2..N */
static pc_uart_ctx_t s_uart1;               /* UART1（标准控制台） */

/*
 * base.instance 编码：
 *   NULL   未分配
 *   (void*)1            UART1
 *   (void*)(index + 2)  UART2 + index
 */
static pc_uart_ctx_t *pc_uart_lookup(hal_uart_t *handle)
{
    uintptr_t instance;

    if (handle == NULL) {
        return NULL;
    }
    instance = (uintptr_t)handle->base.instance;
    if (instance == 1U) {
        return (s_uart1.used && s_uart1.owner == handle) ? &s_uart1 : NULL;
    }
    if (instance >= 2U) {
        uintptr_t index = instance - 2U;

        if (index < PC_UART_MAX && s_uart[index].used &&
            s_uart[index].owner == handle) {
            return &s_uart[index];
        }
    }
    return NULL;
}

static pc_uart_ctx_t *pc_uart_alloc(hal_uart_t *handle)
{
    uintptr_t instance;
    int index;

    if (handle == NULL) {
        return NULL;
    }

    /* 已分配过则直接复用 */
    {
        pc_uart_ctx_t *ctx = pc_uart_lookup(handle);
        if (ctx != NULL) {
            return ctx;
        }
    }

    instance = (uintptr_t)handle->base.instance;
    if (instance == 1U) {
        if (s_uart1.used && s_uart1.owner != handle) {
            return NULL;
        }
        if (!s_uart1.used) {
            memset(&s_uart1, 0, sizeof(s_uart1));
            s_uart1.used = true;
            s_uart1.owner = handle;
            s_uart1.stdio = true;
        }
        return &s_uart1;
    }

    if (instance >= 2U) {
        index = (int)(instance - 2U);
        if (index >= PC_UART_MAX) {
            return NULL;
        }
        if (s_uart[index].used && s_uart[index].owner != handle) {
            return NULL;
        }
        if (!s_uart[index].used) {
            memset(&s_uart[index], 0, sizeof(s_uart[index]));
            s_uart[index].used = true;
            s_uart[index].owner = handle;
        }
        return &s_uart[index];
    }

    /* instance == NULL：自动分配第一个空闲虚拟串口 */
    for (index = 0; index < PC_UART_MAX; index++) {
        if (!s_uart[index].used) {
            memset(&s_uart[index], 0, sizeof(s_uart[index]));
            s_uart[index].used = true;
            s_uart[index].owner = handle;
            handle->base.instance = (void *)(uintptr_t)(index + 2);
            return &s_uart[index];
        }
    }
    return NULL;
}

static void pc_uart_emit_tx(hal_uart_t *handle, size_t length)
{
    if (handle->callback != NULL) {
        handle->callback(handle, HAL_UART_EVENT_TX_COMPLETE, length, handle->user_data);
    }
}

static size_t pc_uart_append_tx(pc_uart_ctx_t *ctx, const uint8_t *data, size_t length)
{
    size_t space = PC_UART_BUF_SIZE - ctx->tx_len;
    size_t n = HAL_MIN(length, space);

    if (n > 0U) {
        memcpy(&ctx->tx[ctx->tx_len], data, n);
        ctx->tx_len += n;
    }
    return n;
}

/* UART1：发送数据同步写到标准输出 */
static void pc_uart_write_stdio(pc_uart_ctx_t *ctx, const uint8_t *data,
                                size_t length)
{
    if (ctx->stdio && length > 0U && data != NULL) {
        fwrite(data, 1U, length, stdout);
        fflush(stdout);
    }
}

static size_t pc_uart_consume_rx(pc_uart_ctx_t *ctx, uint8_t *data, size_t length)
{
    size_t n = HAL_MIN(length, ctx->rx_len);

    if (n > 0U) {
        memcpy(data, ctx->rx, n);
        if (n < ctx->rx_len) {
            memmove(ctx->rx, &ctx->rx[n], ctx->rx_len - n);
        }
        ctx->rx_len -= n;
    }
    return n;
}

/* UART1：把标准输入的非阻塞数据搬进接收缓冲，并回显可见字符 */
static void pc_uart_poll_stdio(pc_uart_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->stdio) {
        return;
    }
#if defined(_WIN32)
    while (_kbhit() && ctx->rx_len < PC_UART_BUF_SIZE) {
        int ch = _getch();

        if (ch == 0 || ch == 224) {         /* 功能键/方向键：吞掉后续字节 */
            (void)_getch();
            continue;
        }
        if (ch == '\r') {                   /* Enter -> 换行 */
            fputs("\r\n", stdout);
            fflush(stdout);
            ctx->rx[ctx->rx_len++] = '\n';
        } else if (ch == '\b') {            /* 退格：擦除并交给 Shell 处理 */
            fputs("\b \b", stdout);
            fflush(stdout);
            ctx->rx[ctx->rx_len++] = '\b';
        } else {
            if (ch >= 32 && ch < 127) {
                fputc(ch, stdout);
                fflush(stdout);
            }
            ctx->rx[ctx->rx_len++] = (char)ch;
        }
    }
#else
    {
        fd_set rfds;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0 &&
            FD_ISSET(STDIN_FILENO, &rfds)) {
            uint8_t tmp[64];
            ssize_t r = read(STDIN_FILENO, tmp, sizeof(tmp));
            size_t i;

            for (i = 0U; i < (size_t)r && ctx->rx_len < PC_UART_BUF_SIZE; i++) {
                ctx->rx[ctx->rx_len++] = tmp[i];
            }
        }
    }
#endif
}

int hal_uart_init(hal_uart_t *handle, const hal_uart_config_t *config)
{
    pc_uart_ctx_t *ctx;
    uintptr_t instance;

    if (handle == NULL || config == NULL) {
        return HAL_INVALID_PARAM;
    }
    instance = (uintptr_t)handle->base.instance;
    if (instance > (uintptr_t)(PC_UART_MAX + 1)) {
        return HAL_INVALID_PARAM;
    }
    ctx = pc_uart_alloc(handle);
    if (ctx == NULL) {
        return HAL_NO_MEMORY;
    }
    handle->config = *config;
    handle->callback = NULL;
    handle->user_data = NULL;
    handle->base.state = HAL_STATE_READY;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_uart_deinit(hal_uart_t *handle)
{
    pc_uart_ctx_t *ctx;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    ctx = pc_uart_lookup(handle);
    if (ctx != NULL) {
        ctx->used = false;
        ctx->stdio = false;
        ctx->owner = NULL;
        ctx->tx_len = 0U;
        ctx->rx_len = 0U;
    }
    handle->base.instance = NULL;
    handle->base.state = HAL_STATE_RESET;
    handle->base.error = HAL_OK;
    return HAL_OK;
}

int hal_uart_transmit(hal_uart_t *handle, const uint8_t *data,
                                 size_t length, uint32_t timeout)
{
    pc_uart_ctx_t *ctx;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    ctx = pc_uart_lookup(handle);
    if (ctx == NULL) {
        return HAL_NOT_READY;
    }
    HAL_UNUSED(timeout);
    pc_uart_append_tx(ctx, data, length);
    pc_uart_write_stdio(ctx, data, length);
    pc_uart_emit_tx(handle, length);
    return HAL_OK;
}

int hal_uart_receive(hal_uart_t *handle, uint8_t *data,
                                size_t length, uint32_t timeout)
{
    pc_uart_ctx_t *ctx;
    size_t n;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_OK;
    }
    ctx = pc_uart_lookup(handle);
    if (ctx == NULL) {
        return HAL_TIMEOUT;
    }
    pc_uart_poll_stdio(ctx);
    if (ctx->rx_len == 0U) {
        return HAL_TIMEOUT;
    }
    n = pc_uart_consume_rx(ctx, data, length);
    if (n == 0U) {
        return HAL_TIMEOUT;
    }
    if (n < length && timeout == 0U) {
        return HAL_TIMEOUT;
    }
    return HAL_OK;
}

int hal_uart_transmit_it(hal_uart_t *handle, const uint8_t *data,
                                    size_t length)
{
    pc_uart_ctx_t *ctx;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    ctx = pc_uart_lookup(handle);
    if (ctx == NULL) {
        return HAL_NOT_READY;
    }
    pc_uart_append_tx(ctx, data, length);
    pc_uart_write_stdio(ctx, data, length);
    pc_uart_emit_tx(handle, length);
    return HAL_OK;
}

int hal_uart_receive_it(hal_uart_t *handle, uint8_t *data,
                                   size_t length)
{
    pc_uart_ctx_t *ctx;
    size_t n;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    if (length == 0U) {
        return HAL_OK;
    }
    ctx = pc_uart_lookup(handle);
    if (ctx == NULL) {
        return HAL_TIMEOUT;
    }
    pc_uart_poll_stdio(ctx);
    if (ctx->rx_len == 0U) {
        return HAL_TIMEOUT;
    }
    n = pc_uart_consume_rx(ctx, data, length);
    if (handle->callback != NULL) {
        handle->callback(handle, HAL_UART_EVENT_RX_COMPLETE, n, handle->user_data);
    }
    return HAL_OK;
}

int hal_uart_transmit_dma(hal_uart_t *handle, const uint8_t *data,
                                     size_t length)
{
    return hal_uart_transmit_it(handle, data, length);
}

int hal_uart_receive_dma(hal_uart_t *handle, uint8_t *data,
                                    size_t length)
{
    return hal_uart_receive_it(handle, data, length);
}

int hal_uart_receive_to_idle(hal_uart_t *handle, uint8_t *data,
                                        size_t length, hal_transfer_mode_e mode)
{
    pc_uart_ctx_t *ctx;
    size_t n;

    if (handle == NULL || (data == NULL && length > 0U)) {
        return HAL_INVALID_PARAM;
    }
    if (handle->base.state == HAL_STATE_RESET) {
        return HAL_NOT_READY;
    }
    HAL_UNUSED(mode);
    if (length == 0U) {
        return HAL_OK;
    }
    ctx = pc_uart_lookup(handle);
    if (ctx == NULL) {
        return HAL_TIMEOUT;
    }
    pc_uart_poll_stdio(ctx);
    if (ctx->rx_len == 0U) {
        return HAL_TIMEOUT;
    }
    n = pc_uart_consume_rx(ctx, data, length);
    if (handle->callback != NULL) {
        handle->callback(handle, HAL_UART_EVENT_RX_IDLE, n, handle->user_data);
    }
    return HAL_OK;
}

size_t hal_uart_get_rx_count(hal_uart_t *handle)
{
    pc_uart_ctx_t *ctx;

    if (handle == NULL) {
        return 0U;
    }
    ctx = pc_uart_lookup(handle);
    if (ctx == NULL) {
        return 0U;
    }
    pc_uart_poll_stdio(ctx);
    return ctx->rx_len;
}

int hal_uart_abort_tx(hal_uart_t *handle)
{
    pc_uart_ctx_t *ctx;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    ctx = pc_uart_lookup(handle);
    if (ctx != NULL) {
        ctx->tx_len = 0U;
    }
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_uart_abort_rx(hal_uart_t *handle)
{
    pc_uart_ctx_t *ctx;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    ctx = pc_uart_lookup(handle);
    if (ctx != NULL) {
        ctx->rx_len = 0U;
    }
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

int hal_uart_abort(hal_uart_t *handle)
{
    pc_uart_ctx_t *ctx;

    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    ctx = pc_uart_lookup(handle);
    if (ctx != NULL) {
        ctx->tx_len = 0U;
        ctx->rx_len = 0U;
    }
    handle->base.state = HAL_STATE_READY;
    return HAL_OK;
}

hal_state_e hal_uart_get_state(hal_uart_t *handle)
{
    if (handle == NULL) {
        return HAL_STATE_RESET;
    }
    return handle->base.state;
}

int hal_uart_get_error(hal_uart_t *handle)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    return handle->base.error;
}

int hal_uart_register_callback(hal_uart_t *handle,
                                          hal_uart_callback_t callback, void *user_data)
{
    if (handle == NULL) {
        return HAL_INVALID_PARAM;
    }
    handle->callback = callback;
    handle->user_data = user_data;
    return HAL_OK;
}

/* ============================== 测试辅助 ============================== */

uint32_t ros_pc_uart_get_tx(hal_uart_t *h, uint8_t *buf, uint32_t cap)
{
    pc_uart_ctx_t *ctx;
    uint32_t n;

    if (h == NULL || buf == NULL) {
        return 0U;
    }
    ctx = pc_uart_lookup(h);
    if (ctx == NULL) {
        return 0U;
    }
    n = (uint32_t)HAL_MIN((size_t)cap, ctx->tx_len);
    if (n > 0U) {
        memcpy(buf, ctx->tx, n);
    }
    return n;
}

uint32_t ros_pc_uart_inject(hal_uart_t *h, const uint8_t *data, uint32_t len)
{
    pc_uart_ctx_t *ctx;
    size_t space;
    size_t n;

    if (h == NULL || (data == NULL && len > 0U)) {
        return 0U;
    }
    ctx = pc_uart_lookup(h);
    if (ctx == NULL) {
        ctx = pc_uart_alloc(h);
    }
    if (ctx == NULL) {
        return 0U;
    }
    space = PC_UART_BUF_SIZE - ctx->rx_len;
    n = HAL_MIN((size_t)len, space);
    if (n > 0U) {
        memcpy(&ctx->rx[ctx->rx_len], data, n);
        ctx->rx_len += n;
    }
    return (uint32_t)n;
}

/* 由 ros_pc_reset_state() 调用，复位 UART1 与全部虚拟串口 */
void pc_uart_reset(void)
{
    memset(s_uart, 0, sizeof(s_uart));
    memset(&s_uart1, 0, sizeof(s_uart1));
}
