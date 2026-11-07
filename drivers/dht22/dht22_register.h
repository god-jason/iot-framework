#ifndef DHT22_REGISTER_H
#define DHT22_REGISTER_H

/*
 * DHT22 单总线时序参数与帧格式。
 *
 * 一次完整通信：
 *   主机: 拉低 >=1ms -> 释放总线（上拉）
 *   从机: 低约 80us -> 高约 80us -> 40 位数据
 *   数据位: 每位先低 50us，随后高 26~28us 表示 0，高 70us 表示 1
 *
 * 帧格式（5 字节，大端）：
 *   [0..1] 湿度 16bit 无符号，单位 0.1%RH
 *   [2..3] 温度 16bit，bit15 为符号位，低 15 位为 0.1℃ 幅值
 *   [4]    校验和 = 前 4 字节累加和的低 8 位
 *
 * 量程：湿度 0~100%RH，温度 -40~80℃。
 */

/* ---------------- 时序（单位 us / ms） ---------------- */

#define DHT22_START_LOW_US          1200U   /* 主机起始拉低（>=1ms） */
#define DHT22_RELEASE_US            30U     /* 释放总线后等待 20~40us */

#define DHT22_LEVEL_TIMEOUT_US      120U    /* 单次电平等待超时（响应 80us/位低 50us） */
#define DHT22_BIT1_MIN_HIGH_US      45U     /* 高电平超过该值判为 1（0: ~27us，1: ~70us） */
#define DHT22_BIT_HIGH_TIMEOUT_US   120U    /* 单比特高电平最长等待 */

#define DHT22_MIN_INTERVAL_MS       2000U   /* 两次采样最小间隔 2s */

/* ---------------- 帧格式 ---------------- */

#define DHT22_FRAME_BYTES           5U

#define DHT22_IDX_HUMI_MSB          0U
#define DHT22_IDX_HUMI_LSB          1U
#define DHT22_IDX_TEMP_MSB          2U
#define DHT22_IDX_TEMP_LSB          3U
#define DHT22_IDX_CHECKSUM          4U

/* ---------------- 数据换算 ---------------- */

#define DHT22_TEMP_SIGN_MASK        0x8000U /* 温度 bit15 为符号位 */
#define DHT22_TEMP_VALUE_MASK       0x7FFFU /* 温度的幅值位 */
#define DHT22_HUMI_LSB              0.1f    /* 湿度 0.1 %RH */
#define DHT22_TEMP_LSB              0.1f    /* 温度 0.1 ℃ */

#endif /* DHT22_REGISTER_H */
