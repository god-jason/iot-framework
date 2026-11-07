#ifndef PORT_DEF_H
#define PORT_DEF_H

/*
 * PORT 基础定义：状态码、句柄基类、回调、通用宏、引脚编码。
 * 本文件不依赖任何 OS 或厂商 HAL。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 通用状态 ---------------- */
typedef enum {
    PORT_OK = 0,
    PORT_ERROR = -1,
    PORT_BUSY = -2,
    PORT_TIMEOUT = -3,
    PORT_INVALID_PARAM = -4,
    PORT_NOT_SUPPORTED = -5,
    PORT_NOT_READY = -6,
    PORT_NO_MEMORY = -7,
    PORT_IO_ERROR = -8,
    PORT_OVERFLOW = -9,
    PORT_ABORTED = -10,
    PORT_NOT_FOUND = -11,
    PORT_ALREADY_INIT = -12,
    PORT_PERMISSION = -13,
    PORT_CRC_ERROR = -14,
    PORT_HW_ERROR = -15
} port_status_t;

/* ---------------- 外设状态 ---------------- */
typedef enum {
    PORT_STATE_RESET = 0,
    PORT_STATE_READY,
    PORT_STATE_BUSY,
    PORT_STATE_BUSY_TX,
    PORT_STATE_BUSY_RX,
    PORT_STATE_BUSY_TX_RX,
    PORT_STATE_ERROR,
    PORT_STATE_SUSPENDED
} port_state_t;

/* ---------------- 传输模式 ---------------- */
typedef enum {
    PORT_MODE_BLOCKING = 0,
    PORT_MODE_IT,
    PORT_MODE_DMA
} port_transfer_mode_t;

/* ---------------- 通用事件 ---------------- */
typedef enum {
    PORT_EVENT_COMPLETE = 0,
    PORT_EVENT_TX_COMPLETE,
    PORT_EVENT_RX_COMPLETE,
    PORT_EVENT_HALF_COMPLETE,
    PORT_EVENT_IDLE,
    PORT_EVENT_ERROR,
    PORT_EVENT_ABORT,
    PORT_EVENT_TIMEOUT
} port_event_t;

/* ---------------- 通用句柄 ---------------- */
typedef struct {
    void *instance;             /* 平台外设实例（寄存器基址 / HAL 句柄指针） */
    void *priv;                 /* 平台私有数据 */

    port_state_t state;
    port_status_t error;

    bool initialized;
} port_handle_base_t;

/* ---------------- 通用回调 ---------------- */
typedef void (*port_callback_t)(void *handle, port_event_t event, void *user_data);

/* ---------------- 超时 ---------------- */
#define PORT_TIMEOUT_FOREVER    UINT32_MAX
#define PORT_TIMEOUT_NONE       0U

/* ---------------- 通用宏 ---------------- */
#define PORT_BIT(n)             (1UL << (n))
#define PORT_ARRAY_SIZE(a)      (sizeof(a) / sizeof((a)[0]))
#define PORT_MIN(a, b)          ((a) < (b) ? (a) : (b))
#define PORT_MAX(a, b)          ((a) > (b) ? (a) : (b))
#define PORT_ALIGN_UP(v, a)     (((v) + (a) - 1U) & ~((a) - 1U))
#define PORT_UNUSED(x)          ((void)(x))

/* ---------------- 逻辑引脚编码 ---------------- */
typedef uint32_t port_pin_t;

#define PORT_PIN(port, pin) \
    ((((uint32_t)(port)) << 8) | ((uint32_t)(pin) & 0xFFU))
#define PORT_PIN_GET_PORT(pin)  (((pin) >> 8) & 0xFFU)
#define PORT_PIN_GET_INDEX(pin) ((pin) & 0xFFU)

/* ---------------- 外设能力标志 ---------------- */
typedef enum {
    PORT_CAP_DMA           = PORT_BIT(0),
    PORT_CAP_IT            = PORT_BIT(1),
    PORT_CAP_CIRCULAR      = PORT_BIT(2),
    PORT_CAP_DOUBLE_BUFFER = PORT_BIT(3),
    PORT_CAP_HALF_TRANSFER = PORT_BIT(4),
    PORT_CAP_IDLE_DETECT   = PORT_BIT(5),
    PORT_CAP_CACHE         = PORT_BIT(6),
    PORT_CAP_FIFO          = PORT_BIT(7)
} port_capability_t;

#ifdef __cplusplus
}
#endif

#endif /* PORT_DEF_H */
