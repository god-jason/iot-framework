#ifndef PORT_CAN_H
#define PORT_CAN_H

/*
 * CAN / CAN-FD 总线：模式与位速率配置、报文收发、过滤器管理。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 PORT_INVALID_PARAM。
 * 经典 CAN 的 payload 上限为 8 字节，CAN-FD 最大 64 字节（见 dlc）。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 工作模式 */
typedef enum {
    PORT_CAN_MODE_NORMAL = 0,       /* 正常收发 */
    PORT_CAN_MODE_LOOPBACK,         /* 内部回环，自发自收 */
    PORT_CAN_MODE_SILENT,           /* 只听模式，不发送应答 */
    PORT_CAN_MODE_SILENT_LOOPBACK   /* 静默回环，用于自测 */
} port_can_mode_t;

/* 标识符类型 */
typedef enum {
    PORT_CAN_ID_STANDARD = 0,   /* 11 位标准帧 ID */
    PORT_CAN_ID_EXTENDED        /* 29 位扩展帧 ID */
} port_can_id_type_t;

/* 帧类型 */
typedef enum {
    PORT_CAN_FRAME_DATA = 0,    /* 数据帧 */
    PORT_CAN_FRAME_REMOTE       /* 远程请求帧 */
} port_can_frame_type_t;

/* CAN 报文 */
typedef struct {
    uint32_t id;                    /* 报文标识符 */

    port_can_id_type_t id_type;     /* 标准/扩展 ID */
    port_can_frame_type_t frame_type;/* 数据帧/远程帧 */

    uint8_t dlc;                    /* 数据长度码：经典 CAN 0~8，FD 0~64 */
    uint8_t data[64];               /* 数据区，仅前 dlc 字节有效 */

    bool fd;                        /* true 表示 CAN-FD 帧 */
    bool brs;                       /* true 表示启用位速率切换（仅 FD） */
} port_can_frame_t;

/* CAN 控制器配置 */
typedef struct {
    uint32_t bitrate;               /* 仲裁段位速率（bps） */
    uint32_t data_bitrate;          /* 数据段位速率（bps），仅 FD 有效 */

    port_can_mode_t mode;           /* 工作模式 */
    bool enable_fd;                 /* 是否启用 CAN-FD */
} port_can_config_t;

/* 接收过滤器 */
typedef struct {
    uint32_t id;                    /* 过滤 ID */
    uint32_t mask;                  /* 掩码：1 表示该位必须匹配 */
    port_can_id_type_t id_type;     /* 过滤的 ID 类型 */
} port_can_filter_t;

/* CAN 句柄 */
typedef struct port_can_handle {
    port_handle_base_t base;        /* 通用句柄基类，勿移动位置 */
    port_can_config_t config;       /* 初始化时保存的配置副本 */

    port_callback_t callback;       /* 事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} port_can_handle_t;

/*
 * 初始化 CAN 控制器（不启动总线）。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL，bitrate 必须大于 0。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；位速率不可达返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_can_init(port_can_handle_t *handle, const port_can_config_t *config);

/* 反初始化 CAN；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_can_deinit(port_can_handle_t *handle);

/* 启动总线（进入可收发状态）；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_can_start(port_can_handle_t *handle);

/* 停止总线；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_can_stop(port_can_handle_t *handle);

/*
 * 阻塞发送一帧。
 * frame   : 报文，不可为 NULL；dlc 超出硬件能力返回 PORT_INVALID_PARAM。
 * timeout : 因总线仲裁失败而等待的最大毫秒数；发送邮箱满返回 PORT_BUSY。
 * 返回    : PORT_OK；PORT_INVALID_PARAM；PORT_TIMEOUT；PORT_BUSY。
 */
port_status_t port_can_transmit(port_can_handle_t *handle, const port_can_frame_t *frame,
                                uint32_t timeout);

/*
 * 阻塞接收一帧。
 * frame   : 输出报文缓冲区，不可为 NULL。
 * timeout : 最大等待毫秒数；PORT_TIMEOUT_FOREVER 表示无限等待。
 * 返回    : PORT_OK；PORT_INVALID_PARAM；PORT_TIMEOUT。
 */
port_status_t port_can_receive(port_can_handle_t *handle, port_can_frame_t *frame,
                               uint32_t timeout);

/* 中断方式发送；frame 仅需在函数返回前有效（实现应立即拷贝） */
port_status_t port_can_transmit_it(port_can_handle_t *handle, const port_can_frame_t *frame);

/*
 * 添加接收过滤器；多次调用按平台容量依次追加。
 * filter : 过滤规则，不可为 NULL。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；过滤器资源耗尽返回 PORT_NO_MEMORY。
 */
port_status_t port_can_add_filter(port_can_handle_t *handle, const port_can_filter_t *filter);

/* 清空全部过滤器；返回 PORT_OK / PORT_INVALID_PARAM */
port_status_t port_can_clear_filters(port_can_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CAN_H */
