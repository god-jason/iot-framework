#ifndef HAL_BASE_H
#define HAL_BASE_H

/*
 * HAL 基础定义：状态码、外设状态、传输方式、通用事件、句柄基类、
 * 超时常量与通用宏。
 *
 * 本文件不依赖任何 OS 或厂商 HAL，各外设头文件只需包含 hal_def.h 即可。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 通用状态 ---------------- */
/* 通用状态码：0 为成功，其余为负值错误码。
 * 状态值统一以 int 返回，枚举仅用于定义常量名，避免跨模块类型转换告警。 */
enum {
    HAL_OK = 0,                /* 成功 */
    HAL_ERROR = -1,            /* 未分类的通用错误 */
    HAL_BUSY = -2,             /* 外设正忙，需等待或稍后重试 */
    HAL_TIMEOUT = -3,          /* 等待超时 */
    HAL_INVALID_PARAM = -4,    /* 参数非法（空指针、越界等） */
    HAL_NOT_SUPPORTED = -5,    /* 平台未实现或模块被裁剪 */
    HAL_NOT_READY = -6,        /* 未初始化或前置条件未满足 */
    HAL_NO_MEMORY = -7,        /* 资源耗尽（如设备表已满） */
    HAL_IO_ERROR = -8,         /* 底层读写失败 */
    HAL_OVERFLOW = -9,         /* 数据溢出（如接收缓冲不足） */
    HAL_ABORTED = -10,         /* 操作被主动中止 */
    HAL_NOT_FOUND = -11,       /* 目标对象不存在 */
    HAL_ALREADY_INIT = -12,    /* 重复初始化或重复注册 */
    HAL_PERMISSION = -13,      /* 受保护资源未解锁或无权限 */
    HAL_CRC_ERROR = -14,       /* 数据校验失败 */
    HAL_HW_ERROR = -15         /* 硬件异常（总线错误等） */
};

/* ---------------- 外设状态 ---------------- */
/* 外设运行状态 */
typedef enum {
    HAL_STATE_RESET = 0,       /* 复位/未初始化 */
    HAL_STATE_READY,           /* 已初始化且空闲 */
    HAL_STATE_BUSY,            /* 忙（方向不明确） */
    HAL_STATE_BUSY_TX,         /* 发送中 */
    HAL_STATE_BUSY_RX,         /* 接收中 */
    HAL_STATE_BUSY_TX_RX,      /* 全双工收发中 */
    HAL_STATE_ERROR,           /* 处于错误态，需反初始化后恢复 */
    HAL_STATE_SUSPENDED        /* 已挂起（DMA 等可恢复） */
} hal_state_e;

/* ---------------- 传输模式 ---------------- */
/* 传输方式 */
typedef enum {
    HAL_MODE_BLOCKING = 0,     /* 阻塞轮询，直到完成或超时 */
    HAL_MODE_IT,               /* 中断方式，完成后触发回调 */
    HAL_MODE_DMA               /* DMA 方式，完成后触发回调 */
} hal_transfer_mode_e;

/* ---------------- 通用事件 ---------------- */
/* 通用事件类型，通过 hal_callback_t 上报 */
typedef enum {
    HAL_EVENT_COMPLETE = 0,    /* 整体传输完成 */
    HAL_EVENT_TX_COMPLETE,     /* 发送完成 */
    HAL_EVENT_RX_COMPLETE,     /* 接收完成 */
    HAL_EVENT_HALF_COMPLETE,   /* 半传输完成（双缓冲前半区） */
    HAL_EVENT_IDLE,            /* 总线空闲（不定长接收结束） */
    HAL_EVENT_ERROR,           /* 传输错误 */
    HAL_EVENT_ABORT,           /* 传输被中止 */
    HAL_EVENT_TIMEOUT          /* 传输超时 */
} hal_event_e;

/* ---------------- 通用句柄 ---------------- */
/*
 * 通用句柄基类。
 * 各外设句柄（hal_xxx_t）的第一个成员必须是本结构，
 * 以便平台层通过强制转换统一处理生命周期与错误上报。
 */
typedef struct {
    void *instance;             /* 平台外设实例（寄存器基址 / HAL 句柄指针 / 平台槽位） */
    hal_state_e state;         /* 当前运行状态，HAL_STATE_RESET 表示未初始化 */
    int error;        /* 最近一次错误码，成功时为 HAL_OK */
} hal_handle_base_t;

/* ---------------- 通用回调 ---------------- */

/*
 * 通用事件回调。
 * handle    : 触发事件的句柄（由实现传入，可能是具体外设句柄指针）；
 * event     : 事件类型；
 * user_data : 注册回调时绑定的用户上下文，可为 NULL。
 * 注意：回调通常在中断上下文执行，禁止阻塞或耗时操作。
 */
typedef void (*hal_callback_t)(void *handle, hal_event_e event, void *user_data);

/* ---------------- 超时 ---------------- */
#define HAL_TIMEOUT_FOREVER    UINT32_MAX  /* 无限等待 */
#define HAL_TIMEOUT_NONE       0U          /* 不等待，立即返回 */

/* ---------------- 通用宏 ---------------- */
#define HAL_BIT(n)             (1UL << (n))                    /* 第 n 位置 1 */
#define HAL_ARRAY_SIZE(a)      (sizeof(a) / sizeof((a)[0]))    /* 数组元素个数 */
#define HAL_MIN(a, b)          ((a) < (b) ? (a) : (b))         /* 取较小值 */
#define HAL_MAX(a, b)          ((a) > (b) ? (a) : (b))         /* 取较大值 */
#define HAL_ALIGN_UP(v, a)     (((v) + (a) - 1U) & ~((a) - 1U))/* 向上对齐到 a 的整数倍，a 必须为 2 的幂 */
#define HAL_UNUSED(x)          ((void)(x))                     /* 显式忽略未使用参数，避免编译告警 */

#ifdef __cplusplus
}
#endif

#endif /* HAL_BASE_H */
