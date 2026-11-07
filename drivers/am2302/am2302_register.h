#ifndef AM2302_REGISTER_H
#define AM2302_REGISTER_H

/*
 * AM2302 电气/时序参数。
 *
 * AM2302（常标注为 DHT22）与 DHT22 使用完全相同的一线制协议：
 * 主机拉低 >=1ms -> 释放 20~40us -> 从机 80us 低 + 80us 高 ->
 * 40 位数据（每位 50us 低 + 高脉宽 27us/70us 表示 0/1）-> 校验和。
 *
 * 为避免两份时序代码分裂，时序常量统一定义在 dht22_register.h，
 * 本文件仅做转发，并给出 AM2302 的器件规格供上层参考：
 *   湿度 0~100%RH（0.1%RH）、温度 -40~80℃（0.1℃，bit15 符号位）、
 *   采样间隔 >=2s。
 */

#include "../dht22/dht22_register.h"

/* 时序（复用 DHT22 常量） */
#define AM2302_START_LOW_US         DHT22_START_LOW_US
#define AM2302_RELEASE_US           DHT22_RELEASE_US
#define AM2302_LEVEL_TIMEOUT_US     DHT22_LEVEL_TIMEOUT_US
#define AM2302_BIT1_MIN_HIGH_US     DHT22_BIT1_MIN_HIGH_US
#define AM2302_BIT_HIGH_TIMEOUT_US  DHT22_BIT_HIGH_TIMEOUT_US
#define AM2302_MIN_INTERVAL_MS      DHT22_MIN_INTERVAL_MS

/* 帧格式（复用 DHT22 定义） */
#define AM2302_FRAME_BYTES          DHT22_FRAME_BYTES
#define AM2302_IDX_HUMI_MSB         DHT22_IDX_HUMI_MSB
#define AM2302_IDX_HUMI_LSB         DHT22_IDX_HUMI_LSB
#define AM2302_IDX_TEMP_MSB         DHT22_IDX_TEMP_MSB
#define AM2302_IDX_TEMP_LSB         DHT22_IDX_TEMP_LSB
#define AM2302_IDX_CHECKSUM         DHT22_IDX_CHECKSUM

/* 数据换算（复用 DHT22 定义） */
#define AM2302_TEMP_SIGN_MASK       DHT22_TEMP_SIGN_MASK
#define AM2302_TEMP_VALUE_MASK      DHT22_TEMP_VALUE_MASK
#define AM2302_HUMI_LSB             DHT22_HUMI_LSB
#define AM2302_TEMP_LSB             DHT22_TEMP_LSB

#endif /* AM2302_REGISTER_H */
