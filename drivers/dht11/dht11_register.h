#ifndef DHT11_REGISTER_H
#define DHT11_REGISTER_H

/*
 * DHT11 单总线时序参数（单位 us）。
 *
 * 一次完整通信（主机拉低 18ms 以上唤醒）：
 *   主机: 低 >=18ms -> 释放总线（上拉）
 *   从机: 低 80us -> 高 80us -> 40 位数据
 *   数据位: 每位先低 50us，随后高 26~28us 表示 0，高 70us 表示 1
 * 数据字节：湿度整数、湿度小数、温度整数、温度小数、校验和
 */

#define DHT11_START_LOW_MS          18U     /* 主机起始拉低时间 */
#define DHT11_RELEASE_US            30U     /* 释放总线后等待 20~40us */

#define DHT11_TIMEOUT_US            120U    /* 单次电平等待超时 */
#define DHT11_BIT_LOW_US            60U     /* 每位起始低电平（约 50us） */
#define DHT11_BIT1_HIGH_US          45U     /* 高电平超过该值判为 1 */
#define DHT11_BIT_TIMEOUT_US        100U    /* 高电平最长等待 */

#define DHT11_FRAME_BYTES           5U
#define DHT11_MIN_INTERVAL_MS       1000U   /* 采样间隔不得小于 1s */

/* 帧字节下标 */
#define DHT11_IDX_HUMI_INT          0U
#define DHT11_IDX_HUMI_DEC          1U
#define DHT11_IDX_TEMP_INT          2U
#define DHT11_IDX_TEMP_DEC          3U
#define DHT11_IDX_CHECKSUM          4U

/* 温度小数字节的符号位（部分兼容器件使用 bit7 表示负温） */
#define DHT11_TEMP_SIGN             0x80U
#define DHT11_DEC_MASK              0x7FU
#define DHT11_DEC_SCALE             0.1f

#endif /* DHT11_REGISTER_H */
