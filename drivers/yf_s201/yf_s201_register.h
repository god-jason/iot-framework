#ifndef YF_S201_REGISTER_H
#define YF_S201_REGISTER_H

/*
 * YF-S201 霍尔水流量传感器参数。
 *
 * 输出为方波脉冲，频率与水流量成正比：
 *   F(Hz) = 7.5 * Q(L/min)，即 Q = F * 60 / pulses_per_liter
 * 出厂标称约 450 脉冲/升（不同批次有偏差，可用校准接口修正）。
 */

#define YF_S201_PULSES_PER_LITER_DEFAULT   450.0f

/* 采样窗（ms）：1s 对应 1Hz 分辨率，与旧实现保持一致 */
#define YF_S201_WINDOW_MS_DEFAULT          1000U
#define YF_S201_WINDOW_MS_MIN              100U
#define YF_S201_WINDOW_MS_MAX              5000U

/* 轮询步进（us）：100us 理论最高可测 5kHz，YF-S201 满量程约 225Hz */
#define YF_S201_SAMPLE_US                  100U

/* 传感器典型量程（L/min） */
#define YF_S201_FLOW_MIN_LPM               1.0f
#define YF_S201_FLOW_MAX_LPM               30.0f

#endif /* YF_S201_REGISTER_H */
