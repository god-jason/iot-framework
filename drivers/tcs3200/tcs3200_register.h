#ifndef TCS3200_REGISTER_H
#define TCS3200_REGISTER_H

/*
 * TCS3200 颜色传感器参数。
 *
 * S0/S1：输出频率分频比（00 掉电，01 2%，10 20%，11 100%），本驱动固定 20%。
 * S2/S3：光电二极管滤波选择（编码值 = (S2 << 1) | S3）：
 *        0 = 红，1 = 蓝，2 = 无滤波（clear），3 = 绿。
 * OUT：方波输出，频率与所选通道光强成正比。
 */

/* 分频设置：S0=1, S1=0 → 20%（兼顾分辨率与可测上限） */
#define TCS3200_SCALE_S0        1U
#define TCS3200_SCALE_S1        0U

/* 输出分频百分比，仅用于文档/上层换算 */
#define TCS3200_SCALE_PERCENT   20U

/* 滤波通道编码 */
#define TCS3200_FILTER_RED      0U
#define TCS3200_FILTER_BLUE     1U
#define TCS3200_FILTER_CLEAR    2U
#define TCS3200_FILTER_GREEN    3U

/* 滤波通道切换后的稳定时间（us） */
#define TCS3200_SETTLE_US       500U

/* 频率测量窗口（us）与轮询步进（us）：窗内统计上升沿数 → Hz = n * 1e6 / window */
#define TCS3200_WINDOW_US       10000U
#define TCS3200_SAMPLE_US       1U

/* 20% 分频下 OUT 频率上限约 120kHz（轮询步进 1us 足以分辨单个周期） */
#define TCS3200_MAX_FREQ_HZ     120000U

#endif /* TCS3200_REGISTER_H */
