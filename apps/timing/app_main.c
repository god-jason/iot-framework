/*
 * app_main.c - PC 平台 ros_sleep_ms 精度测试（实时模式）
 *
 * 本目标定义 ROS_PC_REAL_TIME：PC 端空闲按墙钟节奏等待，1 tick ≈ 1ms 真实时间。
 * 用高精度墙钟（Windows QPC / POSIX CLOCK_MONOTONIC）测量：
 *   - 每次请求时长的实际耗时 min/avg/max；
 *   - 对应内核 tick 的推进量（验证虚拟时间是否精确）；
 *   - 连续 1ms 睡眠的累计漂移。
 *
 * 运行：xmake b ros-timing && xmake r ros-timing
 */

#include "ros.h"
#include "app.h"
#include "pc.h"

#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>

static uint64_t wall_us(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;

    if (freq.QuadPart == 0) {
        (void)QueryPerformanceFrequency(&freq);
    }
    (void)QueryPerformanceCounter(&c);
    return (uint64_t)((c.QuadPart * 1000000LL) / freq.QuadPart);
}
#else
#include <time.h>

static uint64_t wall_us(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000L);
}
#endif

#define TM_1MS_N      50U
#define TM_5MS_N      20U
#define TM_10MS_N     10U
#define TM_20MS_N      6U
#define TM_50MS_N      4U
#define TM_100MS_N     3U
#define TM_DRIFT_N   300U

static void measure(uint32_t req_ms, uint32_t n)
{
    uint32_t i;
    uint32_t wmin = 0xFFFFFFFFu;
    uint32_t wmax = 0u;
    uint32_t wsum = 0u;
    uint32_t tmin = 0xFFFFFFFFu;
    uint32_t tmax = 0u;
    uint32_t tsum = 0u;

    for (i = 0u; i < n; i++) {
        uint32_t tick0 = ros_tick_get();
        uint64_t w0 = wall_us();
        uint64_t w1;
        uint32_t dt;
        uint32_t dticks;

        ros_sleep_ms(req_ms);
        w1 = wall_us();
        dt = (uint32_t)(w1 - w0);
        dticks = ros_tick_get() - tick0;

        if (dt < wmin) {
            wmin = dt;
        }
        if (dt > wmax) {
            wmax = dt;
        }
        wsum += dt;
        if (dticks < tmin) {
            tmin = dticks;
        }
        if (dticks > tmax) {
            tmax = dticks;
        }
        tsum += dticks;
    }

    printf("req=%-4ums n=%-3u wall min/avg/max = %u/%u/%u us | tick min/avg/max = %u/%u/%u\n",
           (unsigned)req_ms, (unsigned)n,
           (unsigned)wmin, (unsigned)(wsum / n), (unsigned)wmax,
           (unsigned)tmin, (unsigned)(tsum / n), (unsigned)tmax);
}

void app_main(void *arg)
{
    uint32_t i;
    uint64_t w0;
    uint64_t w1;
    uint32_t target;

    (void)arg;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== ros_sleep_ms timing (PC real-time) ===\n");

    /* 打开实时模式：模拟 tick 跟随墙钟，ros_sleep_ms 真实等待 */
    ros_pc_set_real_time(true);

    measure(1U, TM_1MS_N);
    measure(5U, TM_5MS_N);
    measure(10U, TM_10MS_N);
    measure(20U, TM_20MS_N);
    measure(50U, TM_50MS_N);
    measure(100U, TM_100MS_N);

    w0 = wall_us();
    for (i = 0u; i < TM_DRIFT_N; i++) {
        ros_sleep_ms(1U);
    }
    w1 = wall_us();
    target = TM_DRIFT_N * 1000U;

    printf("drift: %u x 1ms -> %u us (target %u, diff %d us)\n",
           (unsigned)TM_DRIFT_N, (unsigned)(uint32_t)(w1 - w0),
           (unsigned)target,
           (int)(uint32_t)(w1 - w0) - (int)target);

    app_set_exit_code(0);
    ros_stop();
}
