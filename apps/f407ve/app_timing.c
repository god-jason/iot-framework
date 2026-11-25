/*
 * app_timing.c - ros_sleep_ms 精度测试
 *
 * 基准：ros_port_trace_time_us()（STM32 平台用 DWT->CYCCNT，µs 分辨率）。
 * 对每个请求时长重复若干次，统计实际耗时的最小/平均/最大值，
 * 再跑一段连续 1ms 睡眠，观察累计漂移（tick 精度 + 唤醒开销）。
 *
 * 注意：DWT 是 32 位周期计数，ros_port_trace_time_us 约每 25.6s
 * （168MHz）回绕一次；本测试单个测量区间远小于该值，用无符号差值安全。
 */

#include "app_timing.h"

#include "ros.h"
#include "log.h"

#define TM_1MS_N      100U
#define TM_5MS_N       30U
#define TM_10MS_N      20U
#define TM_20MS_N      10U
#define TM_50MS_N       5U
#define TM_100MS_N      3U
#define TM_DRIFT_N    300U

/* 测量 req_ms 睡眠 n 次的实际耗时分布 */
static void tm_measure(uint32_t req_ms, uint32_t n)
{
    uint32_t i;
    uint32_t min = 0xFFFFFFFFu;
    uint32_t max = 0u;
    uint32_t sum = 0u;
    uint32_t tmin = 0xFFFFFFFFu;
    uint32_t tmax = 0u;
    uint32_t tsum = 0u;
    uint32_t target = req_ms * 1000U;

    for (i = 0U; i < n; i++) {
        uint32_t tick0 = ros_tick_get();
        uint32_t t0 = ros_port_trace_time_us();
        uint32_t dt;
        uint32_t dticks;

        ros_sleep_ms(req_ms);
        dt = ros_port_trace_time_us() - t0;
        dticks = ros_tick_get() - tick0;
        if (dt < min) {
            min = dt;
        }
        if (dt > max) {
            max = dt;
        }
        sum += dt;
        if (dticks < tmin) {
            tmin = dticks;
        }
        if (dticks > tmax) {
            tmax = dticks;
        }
        tsum += dticks;
    }

    {
        uint32_t avg = sum / n;
        const char *sign = (avg < target) ? "-" : "+";
        uint32_t err = (avg < target) ? (target - avg) : (avg - target);

        LOG_I("tm", "req=%ums n=%u: wall %u/%u/%u us (err=%s%u), tick %u/%u/%u",
              (unsigned)req_ms, (unsigned)n, (unsigned)min, (unsigned)avg,
              (unsigned)max, sign, (unsigned)err,
              (unsigned)tmin, (unsigned)(tsum / n), (unsigned)tmax);
    }
}

void app_timing_run(void)
{
    static uint8_t done;
    uint32_t i;
    uint32_t t0;
    uint32_t dt;
    uint32_t target;
    uint32_t tk0;

    if (done != 0U) {
        return;
    }
    done = 1U;

    LOG_I("tm", "ros_sleep_ms timing test (DWT us base)");

    tm_measure(1U, TM_1MS_N);
    tm_measure(5U, TM_5MS_N);
    tm_measure(10U, TM_10MS_N);
    tm_measure(20U, TM_20MS_N);
    tm_measure(50U, TM_50MS_N);
    tm_measure(100U, TM_100MS_N);

    /* 累计漂移：连续 1ms 睡眠 */
    tk0 = ros_tick_get();
    t0 = ros_port_trace_time_us();
    for (i = 0U; i < TM_DRIFT_N; i++) {
        ros_sleep_ms(1U);
    }
    dt = ros_port_trace_time_us() - t0;
    target = TM_DRIFT_N * 1000U;

    {
        const char *sign = (dt < target) ? "-" : "+";
        uint32_t err = (dt < target) ? (target - dt) : (dt - target);

        LOG_I("tm", "drift: %u x 1ms -> %u us (target %u, err=%s%u us), ticks=%u",
              (unsigned)TM_DRIFT_N, (unsigned)dt, (unsigned)target,
              sign, (unsigned)err, (unsigned)(ros_tick_get() - tk0));
    }
}
