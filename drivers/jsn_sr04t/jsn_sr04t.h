#ifndef JSN_SR04T_H
#define JSN_SR04T_H

/*
 * JSN-SR04T 防水超声波测距模块（TRIG 触发 + ECHO 回响）。
 *
 * 参考 libdriver 风格：jsn_sr04t_init / jsn_sr04t_deinit / jsn_sr04t_read
 * ECHO 高电平时长与距离成正比：distance_m = echo_us / 5800。
 *
 * 说明：driver_io 层暂无输入捕获抽象，测量用 driver_delay_us + GPIO 轮询
 * 近似计时，精度受调度/中断影响；高精度场合应改用 port_capture。
 */

#include "common/driver_io.h"
#include "jsn_sr04t_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    port_pin_t trig;
    port_pin_t echo;
    uint32_t timeout_us;
    bool initialized;
} jsn_sr04t_handle_t;

/**
 * @brief  初始化：TRIG 推挽输出（低）、ECHO 输入（下拉）
 * @param  trig  TRIG 引脚逻辑编号，0 视为无效
 * @param  echo  ECHO 引脚逻辑编号，0 视为无效
 */
int jsn_sr04t_init(jsn_sr04t_handle_t *handle, port_pin_t trig, port_pin_t echo);

int jsn_sr04t_deinit(jsn_sr04t_handle_t *handle);

/** 单次测量，返回距离（m）；超出 JSN_SR04T_MAX_RANGE_M 返回 PORT_OVERFLOW */
int jsn_sr04t_read(jsn_sr04t_handle_t *handle, float *distance_m);

/** 单次测量，返回 ECHO 高电平时长（us），便于上层自行换算/滤波 */
int jsn_sr04t_read_us(jsn_sr04t_handle_t *handle, uint32_t *echo_us);

/** 设置 ECHO 等待超时（us） */
int jsn_sr04t_set_timeout(jsn_sr04t_handle_t *handle, uint32_t timeout_us);

#ifdef __cplusplus
}
#endif

#endif /* JSN_SR04T_H */
