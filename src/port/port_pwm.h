#ifndef PORT_PWM_H
#define PORT_PWM_H

/*
 * PWM 输出：频率、占空比与极性配置，启停及运行期动态调整。
 * 底层由定时器通道实现，通道号由平台在句柄中指定。
 *
 * 句柄由调用者静态分配，base 必须是首个成员。
 * 所有接口在 handle 为 NULL 时返回 PORT_INVALID_PARAM。
 */

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 输出极性 */
typedef enum {
    PORT_PWM_POLARITY_HIGH = 0, /* 高电平有效（占空比部分为高） */
    PORT_PWM_POLARITY_LOW       /* 低电平有效 */
} port_pwm_polarity_t;

/* PWM 配置 */
typedef struct {
    uint32_t frequency;         /* PWM 频率（Hz），必须大于 0 */
    uint32_t duty;              /* 占空比，范围 0~10000，对应 0~100.00% */

    port_pwm_polarity_t polarity; /* 输出极性 */
} port_pwm_config_t;

/* PWM 句柄 */
typedef struct port_pwm_handle {
    port_handle_base_t base;        /* 通用句柄基类，勿移动位置 */

    port_pwm_config_t config;       /* 初始化时保存的配置副本 */

    uint8_t channel;                /* 定时器通道号，由平台/调用者预先设置 */
} port_pwm_handle_t;

/*
 * 初始化 PWM 通道。
 * handle : 句柄，不可为 NULL，调用前需设置 channel。
 * config : 配置，不可为 NULL；duty 超过 10000 视为 10000。
 * 返回   : PORT_OK；PORT_INVALID_PARAM；频率不可达返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_pwm_init(port_pwm_handle_t *handle, const port_pwm_config_t *config);

/* 反初始化 PWM 通道并停止输出；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_pwm_deinit(port_pwm_handle_t *handle);

/* 启动 PWM 输出；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_pwm_start(port_pwm_handle_t *handle);

/* 停止 PWM 输出（通道保持无效电平）；返回 PORT_OK / PORT_INVALID_PARAM / PORT_NOT_READY */
port_status_t port_pwm_stop(port_pwm_handle_t *handle);

/*
 * 运行期修改频率。
 * frequency : 目标频率（Hz），必须大于 0；会同步重算占空比寄存器。
 * 返回      : PORT_OK；PORT_INVALID_PARAM；不可达返回 PORT_NOT_SUPPORTED。
 */
port_status_t port_pwm_set_frequency(port_pwm_handle_t *handle, uint32_t frequency);

/*
 * 运行期修改占空比。
 * duty : 0~10000 对应 0~100.00%，超过 10000 按 10000 处理。
 * 返回 : PORT_OK；PORT_INVALID_PARAM（句柄为空）。
 */
port_status_t port_pwm_set_duty(port_pwm_handle_t *handle, uint32_t duty);

/* 运行期修改输出极性；返回 PORT_OK / PORT_INVALID_PARAM */
port_status_t port_pwm_set_polarity(port_pwm_handle_t *handle, port_pwm_polarity_t polarity);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PWM_H */
