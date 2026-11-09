#ifndef HAL_CAN_H
#define HAL_CAN_H

/*
 * CAN / CAN-FD 总线：模式与位速率配置、报文收发、过滤器管理。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 HAL_INVALID_PARAM。
 * 经典 CAN 的 payload 上限为 8 字节，CAN-FD 最大 64 字节（见 dlc）。
 */

#include "hal_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 工作模式 */
typedef enum {
    HAL_CAN_MODE_NORMAL = 0,       /* 正常收发 */
    HAL_CAN_MODE_LOOPBACK,         /* 内部回环，自发自收 */
    HAL_CAN_MODE_SILENT,           /* 只听模式，不发送应答 */
    HAL_CAN_MODE_SILENT_LOOPBACK   /* 静默回环，用于自测 */
} hal_can_mode_e;

/* 标识符类型 */
typedef enum {
    HAL_CAN_ID_STANDARD = 0,   /* 11 位标准帧 ID */
    HAL_CAN_ID_EXTENDED        /* 29 位扩展帧 ID */
} hal_can_id_type_e;

/* 帧类型 */
typedef enum {
    HAL_CAN_FRAME_DATA = 0,    /* 数据帧 */
    HAL_CAN_FRAME_REMOTE       /* 远程请求帧 */
} hal_can_frame_type_e;

/* CAN 报文 */
typedef struct {
    uint32_t id;                    /* 报文标识符 */

    hal_can_id_type_e id_type;     /* 标准/扩展 ID */
    hal_can_frame_type_e frame_type;/* 数据帧/远程帧 */

    uint8_t dlc;                    /* 数据长度码：经典 CAN 0~8，FD 0~64 */
    uint8_t data[64];               /* 数据区，仅前 dlc 字节有效 */

    bool fd;                        /* true 表示 CAN-FD 帧 */
    bool brs;                       /* true 表示启用位速率切换（仅 FD） */
} hal_can_frame_t;

/* CAN 控制器配置 */
typedef struct {
    uint32_t bitrate;               /* 仲裁段位速率（bps） */
    uint32_t data_bitrate;          /* 数据段位速率（bps），仅 FD 有效 */

    hal_can_mode_e mode;           /* 工作模式 */
    bool enable_fd;                 /* 是否启用 CAN-FD */
} hal_can_config_t;

/* 接收过滤器 */
typedef struct {
    uint32_t id;                    /* 过滤 ID */
    uint32_t mask;                  /* 掩码：1 表示该位必须匹配 */
    hal_can_id_type_e id_type;     /* 过滤的 ID 类型 */
} hal_can_filter_t;

/* CAN 句柄 */
typedef struct hal_can {
    hal_handle_base_t base;        /* 通用句柄基类，勿移动位置 */
    hal_can_config_t config;       /* 初始化时保存的配置副本 */

    hal_callback_t callback;       /* 事件回调，可为 NULL */
    void *user_data;                /* 回调上下文，可为 NULL */
} hal_can_t;

/*
 * 初始化 CAN 控制器（不启动总线）。
 * handle : 句柄，不可为 NULL。
 * config : 配置，不可为 NULL，bitrate 必须大于 0。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；位速率不可达返回 HAL_NOT_SUPPORTED。
 */
int hal_can_init(hal_can_t *handle, const hal_can_config_t *config);

/* 反初始化 CAN；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
int hal_can_deinit(hal_can_t *handle);

/* 启动总线（进入可收发状态）；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
int hal_can_start(hal_can_t *handle);

/* 停止总线；返回 HAL_OK / HAL_INVALID_PARAM / HAL_NOT_READY */
int hal_can_stop(hal_can_t *handle);

/*
 * 阻塞发送一帧。
 * frame   : 报文，不可为 NULL；dlc 超出硬件能力返回 HAL_INVALID_PARAM。
 * timeout : 因总线仲裁失败而等待的最大毫秒数；发送邮箱满返回 HAL_BUSY。
 * 返回    : HAL_OK；HAL_INVALID_PARAM；HAL_TIMEOUT；HAL_BUSY。
 */
int hal_can_transmit(hal_can_t *handle, const hal_can_frame_t *frame,
                                uint32_t timeout);

/*
 * 阻塞接收一帧。
 * frame   : 输出报文缓冲区，不可为 NULL。
 * timeout : 最大等待毫秒数；HAL_TIMEOUT_FOREVER 表示无限等待。
 * 返回    : HAL_OK；HAL_INVALID_PARAM；HAL_TIMEOUT。
 */
int hal_can_receive(hal_can_t *handle, hal_can_frame_t *frame,
                               uint32_t timeout);

/* 中断方式发送；frame 仅需在函数返回前有效（实现应立即拷贝） */
int hal_can_transmit_it(hal_can_t *handle, const hal_can_frame_t *frame);

/*
 * 添加接收过滤器；多次调用按平台容量依次追加。
 * filter : 过滤规则，不可为 NULL。
 * 返回   : HAL_OK；HAL_INVALID_PARAM；过滤器资源耗尽返回 HAL_NO_MEMORY。
 */
int hal_can_add_filter(hal_can_t *handle, const hal_can_filter_t *filter);

/* 清空全部过滤器；返回 HAL_OK / HAL_INVALID_PARAM */
int hal_can_clear_filters(hal_can_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_H */
