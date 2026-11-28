#ifndef APP_TIMING_H
#define APP_TIMING_H

/*
 * app_timing.h - ros_sleep_ms 精度测试
 *
 * APP_TIMING_ENABLE = 1：编译并在启动阶段同步执行一次；
 * APP_TIMING_ENABLE = 0：不参与编译。
 *
 * 以 DWT 周期计数器（ros_port_trace_time_us，µs 分辨率）为基准，
 * 统计不同请求时长下的实际最小/平均/最大值与累计漂移。
 * 放在压测启动之前执行，避免高优先级任务抢占带来的额外抖动。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 默认关闭：仅调试目标开启 */
#ifndef APP_TIMING_ENABLE
#define APP_TIMING_ENABLE 0
#endif

/* 同步执行一次延时精度测试并打印结果；重复调用无副作用 */
void app_timing_run(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TIMING_H */
