#ifndef PORT_DEF_H
#define PORT_DEF_H

/*
 * PORT 基础定义：状态码、句柄基类、回调、通用宏、引脚编码。
 * 本文件不依赖任何 OS 或厂商 HAL。
 *
 * 通用约定：
 *   - 所有返回 port_status_t 的接口，PORT_OK(0) 表示成功，负值表示失败；
 *   - 外设句柄由调用者静态分配并提供，PORT 层内部不申请堆内存；
 *   - 句柄的 error 字段保存最近一次错误码，调用成功时由实现复位为 PORT_OK；
 *   - 传入句柄的接口在 handle 为 NULL 时统一返回 PORT_INVALID_PARAM。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 通用状态 ---------------- */
/* 通用状态码：0 为成功，其余为负值错误码 */
typedef enum {
    PORT_OK = 0,                /* 成功 */
    PORT_ERROR = -1,            /* 未分类的通用错误 */
    PORT_BUSY = -2,             /* 外设正忙，需等待或稍后重试 */
    PORT_TIMEOUT = -3,          /* 等待超时 */
    PORT_INVALID_PARAM = -4,    /* 参数非法（空指针、越界等） */
    PORT_NOT_SUPPORTED = -5,    /* 平台未实现或模块被裁剪 */
    PORT_NOT_READY = -6,        /* 未初始化或前置条件未满足 */
    PORT_NO_MEMORY = -7,        /* 资源耗尽（如设备表已满） */
    PORT_IO_ERROR = -8,         /* 底层读写失败 */
    PORT_OVERFLOW = -9,         /* 数据溢出（如接收缓冲不足） */
    PORT_ABORTED = -10,         /* 操作被主动中止 */
    PORT_NOT_FOUND = -11,       /* 目标对象不存在 */
    PORT_ALREADY_INIT = -12,    /* 重复初始化或重复注册 */
    PORT_PERMISSION = -13,      /* 受保护资源未解锁或无权限 */
    PORT_CRC_ERROR = -14,       /* 数据校验失败 */
    PORT_HW_ERROR = -15         /* 硬件异常（总线错误等） */
} port_status_t;

/* ---------------- 外设状态 ---------------- */
/* 外设运行状态 */
typedef enum {
    PORT_STATE_RESET = 0,       /* 复位/未初始化 */
    PORT_STATE_READY,           /* 已初始化且空闲 */
    PORT_STATE_BUSY,            /* 忙（方向不明确） */
    PORT_STATE_BUSY_TX,         /* 发送中 */
    PORT_STATE_BUSY_RX,         /* 接收中 */
    PORT_STATE_BUSY_TX_RX,      /* 全双工收发中 */
    PORT_STATE_ERROR,           /* 处于错误态，需反初始化后恢复 */
    PORT_STATE_SUSPENDED        /* 已挂起（DMA 等可恢复） */
} port_state_t;

/* ---------------- 传输模式 ---------------- */
/* 传输方式 */
typedef enum {
    PORT_MODE_BLOCKING = 0,     /* 阻塞轮询，直到完成或超时 */
    PORT_MODE_IT,               /* 中断方式，完成后触发回调 */
    PORT_MODE_DMA               /* DMA 方式，完成后触发回调 */
} port_transfer_mode_t;

/* ---------------- 通用事件 ---------------- */
/* 通用事件类型，通过 port_callback_t 上报 */
typedef enum {
    PORT_EVENT_COMPLETE = 0,    /* 整体传输完成 */
    PORT_EVENT_TX_COMPLETE,     /* 发送完成 */
    PORT_EVENT_RX_COMPLETE,     /* 接收完成 */
    PORT_EVENT_HALF_COMPLETE,   /* 半传输完成（双缓冲前半区） */
    PORT_EVENT_IDLE,            /* 总线空闲（不定长接收结束） */
    PORT_EVENT_ERROR,           /* 传输错误 */
    PORT_EVENT_ABORT,           /* 传输被中止 */
    PORT_EVENT_TIMEOUT          /* 传输超时 */
} port_event_t;

/* ---------------- 通用句柄 ---------------- */
/*
 * 通用句柄基类。
 * 各外设句柄（port_xxx_handle_t）的第一个成员必须是本结构，
 * 以便平台层通过强制转换统一处理生命周期与错误上报。
 */
typedef struct {
    void *instance;             /* 平台外设实例（寄存器基址 / HAL 句柄指针） */
    void *priv;                 /* 平台私有数据，由平台实现自行解释 */

    port_state_t state;         /* 当前运行状态 */
    port_status_t error;        /* 最近一次错误码，成功时为 PORT_OK */

    bool initialized;           /* 是否已完成初始化 */
} port_handle_base_t;

/* ---------------- 通用回调 ---------------- */

/*
 * 通用事件回调。
 * handle    : 触发事件的句柄（由实现传入，可能是具体外设句柄指针）；
 * event     : 事件类型；
 * user_data : 注册回调时绑定的用户上下文，可为 NULL。
 * 注意：回调通常在中断上下文执行，禁止阻塞或耗时操作。
 */
typedef void (*port_callback_t)(void *handle, port_event_t event, void *user_data);

/* ---------------- 超时 ---------------- */
#define PORT_TIMEOUT_FOREVER    UINT32_MAX  /* 无限等待 */
#define PORT_TIMEOUT_NONE       0U          /* 不等待，立即返回 */

/* ---------------- 通用宏 ---------------- */
#define PORT_BIT(n)             (1UL << (n))                    /* 第 n 位置 1 */
#define PORT_ARRAY_SIZE(a)      (sizeof(a) / sizeof((a)[0]))    /* 数组元素个数 */
#define PORT_MIN(a, b)          ((a) < (b) ? (a) : (b))         /* 取较小值 */
#define PORT_MAX(a, b)          ((a) > (b) ? (a) : (b))         /* 取较大值 */
#define PORT_ALIGN_UP(v, a)     (((v) + (a) - 1U) & ~((a) - 1U))/* 向上对齐到 a 的整数倍，a 必须为 2 的幂 */
#define PORT_UNUSED(x)          ((void)(x))                     /* 显式忽略未使用参数，避免编译告警 */

/* ---------------- 逻辑引脚编码 ----------------
 * 高 8 位为端口号，低 8 位为引脚号；具体端口号到硬件端口的映射由平台决定。
 */
typedef uint32_t port_pin_t;

/* 由端口号与引脚号构造逻辑引脚编码 */
#define PORT_PIN(port, pin) \
    ((((uint32_t)(port)) << 8) | ((uint32_t)(pin) & 0xFFU))
#define PORT_PIN_GET_PORT(pin)  (((pin) >> 8) & 0xFFU)  /* 提取端口号 */
#define PORT_PIN_GET_INDEX(pin) ((pin) & 0xFFU)          /* 提取引脚号 */

/* ---------------- 外设能力标志 ----------------
 * 位掩码形式，可按位或组合；由 port_get_capabilities() 返回。
 */
typedef enum {
    PORT_CAP_DMA           = PORT_BIT(0),   /* 支持 DMA 传输 */
    PORT_CAP_IT            = PORT_BIT(1),   /* 支持中断方式传输 */
    PORT_CAP_CIRCULAR      = PORT_BIT(2),   /* 支持循环（环形）模式 */
    PORT_CAP_DOUBLE_BUFFER = PORT_BIT(3),   /* 支持双缓冲 */
    PORT_CAP_HALF_TRANSFER = PORT_BIT(4),   /* 支持半传输事件 */
    PORT_CAP_IDLE_DETECT   = PORT_BIT(5),   /* 支持总线空闲检测 */
    PORT_CAP_CACHE         = PORT_BIT(6),   /* 需要/支持缓存维护操作 */
    PORT_CAP_FIFO          = PORT_BIT(7)    /* 具备硬件 FIFO */
} port_capability_t;

#ifdef __cplusplus
}
#endif

#endif /* PORT_DEF_H */
