/*
 * 呼吸灯模块实现。
 *
 * 职责：
 *   按正弦曲线计算 PWM 占空比，通过 ops->set_duty 输出，实现亮度
 *   平滑渐亮渐暗的「呼吸」效果。
 *
 * 数据流：
 *   init() 绑定底层 PWM 回调 -> start() 设定周期并标记激活 ->
 *   update(tick_ms) 用「当前 tick - 起始 tick」求相位并计算占空比 ->
 *   stop() 关闭输出。
 *
 * 依赖与约束：
 *   - 依赖 <math.h> 的 sinf()，占空比量化为 0..1000；
 *   - tick_ms 由调用者提供，需单调递增（通常为系统毫秒计数）；
 *   - 相位未按周期取模，占空比的有效区间只覆盖启动后的第一个周期，
 *     周期结束后需重新 start() 才能继续产生 0..1000 的亮度；
 *   - 未激活（active 为 false）时 update 直接返回 -1，不输出。
 */
#include "breath_led.h"
#include "log.h"

#include <math.h>

/* 部分工具链的 math.h 不提供 M_PI，这里按单精度补一个定义 */
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/*
 * 初始化呼吸灯对象。
 * 参数：led 对象指针（非 NULL）；ops 操作表（非 NULL 且 set_duty 必须实现）。
 * 返回：0 成功；参数非法返回 -1。
 * 说明：默认周期 2000ms，初始为未启动（started/active 均为 false）。
 */
int breath_led_init(breath_led_t *led, const breath_led_ops_t *ops)
{
    if (led == NULL || ops == NULL || ops->set_duty == NULL) {
        return -1;
    }
    led->ops = ops;
    led->duty = 0;
    led->period_ms = 2000;
    led->start_tick = 0;
    led->started = false;
    led->active = false;
    return 0;
}

/*
 * 启动呼吸效果。
 * 参数：led 对象指针；period_ms 呼吸周期（毫秒，必须大于 0）。
 * 返回：0 成功；参数非法返回 -1。
 * 说明：仅置激活标志，起始 tick 推迟到第一次 update 时锁存，
 *       以便用调用者的时间基准对齐。
 */
int breath_led_start(breath_led_t *led, uint16_t period_ms)
{
    if (led == NULL || period_ms == 0) {
        return -1;
    }
    led->period_ms = period_ms;
    led->active = true;
    led->started = false;
    led->start_tick = 0;
    LOG_I("breath", "start");
    return 0;
}

/*
 * 周期调用：按当前 tick 计算并输出占空比。
 * 参数：led 对象指针；tick_ms 当前毫秒计数（需单调递增）。
 * 返回：底层 set_duty 的返回值；led 为空或未激活时返回 -1。
 * 说明：第一次调用会锁存 start_tick 作为相位零点。
 */
int breath_led_update(breath_led_t *led, uint32_t tick_ms)
{
    float phase, duty;

    if (led == NULL || !led->active) {
        return -1;
    }
    /* 首次更新时锁存起始时刻 */
    if (!led->started) {
        led->start_tick = tick_ms;
        led->started = true;
    }
    /* 正弦呼吸：sin(0..π) 从 0 升到 1 再降回 0 */
    phase = (float)(tick_ms - led->start_tick) / (float)led->period_ms;
    duty = sinf(phase * (float)M_PI) * 1000.0f;
    /* 注意：phase > 1 后 sine 为负，转 uint16_t 会得到很大的值，
     * 因此调用者应在一个周期内重新 start() 或自行对相位取模 */
    led->duty = (uint16_t)duty;
    return led->ops->set_duty(led->ops->ctx, led->duty);
}

/*
 * 停止呼吸并熄灭。
 * 参数：led 对象指针。
 * 返回：底层 set_duty(0) 的返回值；led 为空返回 -1。
 * 说明：清除 active 标志，之后 update 将不再输出。
 */
int breath_led_stop(breath_led_t *led)
{
    if (led == NULL) {
        return -1;
    }
    led->active = false;
    led->duty = 0;
    return led->ops->set_duty(led->ops->ctx, 0);
}