#ifndef SRF05_H
#define SRF05_H

/*
 * SRF05 超声波测距模块（TRIG 触发 + ECHO 回响，2 线模式）。
 *
 * 参考 libdriver 风格：srf05_init / srf05_deinit / srf05_read
 * MODE 引脚需悬空（默认 2 线模式）；单线模式下触发的实现不在此驱动内。
 * ECHO 高电平时长与距离成正比：distance_m = echo_us / 5800。
 *
 * 说明：driver_io 层暂无输入捕获抽象，测量用 driver_delay_us + GPIO 轮询
 * 近似计时，精度受调度/中断影响；高精度场合应改用 port_capture。
 */

#include "common/driver_io.h"
#include "srf05_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    port_pin_t trig;
    port_pin_t echo;
    uint32_t timeout_us;
    bool initialized;
} srf05_handle_t;

/**
 * @brief  初始化：TRIG 推挽输出（低）、ECHO 输入（下拉）
 * @param  trig  TRIG 引脚逻辑编号，0 视为无效
 * @param  echo  ECHO 引脚逻辑编号，0 视为无效
 */
int srf05_init(srf05_handle_t *handle, port_pin_t trig, port_pin_t echo);

int srf05_deinit(srf05_handle_t *handle);

/** 单次测量，返回距离（m）；超出 SRF05_MAX_RANGE_M 返回 PORT_OVERFLOW */
int srf05_read(srf05_handle_t *handle, float *distance_m);

/** 单次测量，返回 ECHO 高电平时长（us），便于上层自行换算/滤波 */
int srf05_read_us(srf05_handle_t *handle, uint32_t *echo_us);

/** 设置 ECHO 等待超时（us） */
int srf05_set_timeout(srf05_handle_t *handle, uint32_t timeout_us);

#ifdef __cplusplus
}
#endif

#endif /* SRF05_H */
