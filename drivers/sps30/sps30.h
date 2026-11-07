#ifndef SPS30_H
#define SPS30_H

/*
 * SPS30 颗粒物传感器驱动（I2C 模式，默认地址 0x69）。
 *
 * 单位：
 *   - pm1 / pm25 / pm4 / pm10 : 质量浓度 µg/m³
 *   - typical_size            : 典型粒径 µm
 *
 * 说明：仅实现 I2C 模式；UART 模式需另做帧解析（本驱动不提供，
 * 以避免在没有 datasheet 依据的情况下编造帧格式）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    bool initialized;
    bool measuring;
} sps30_handle_t;

typedef struct {
    float pm1;           /* PM1.0  µg/m³ */
    float pm25;          /* PM2.5  µg/m³ */
    float pm4;           /* PM4.0  µg/m³ */
    float pm10;          /* PM10   µg/m³ */
    float typical_size;  /* 典型粒径 µm */
} sps30_data_t;

/** @param address 7 位地址，传 0 取 0x69 */
int sps30_init(sps30_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

int sps30_deinit(sps30_handle_t *handle);

/** 启动连续测量（float 输出格式） */
int sps30_start(sps30_handle_t *handle);

int sps30_stop(sps30_handle_t *handle);

/** 触发风扇自清洁（约 10s，期间测量值无效） */
int sps30_start_fan_cleaning(sps30_handle_t *handle);

/** 读版本号（ID 校验用） */
int sps30_get_version(sps30_handle_t *handle, uint8_t *major, uint8_t *minor);

/** 读序列号字符串（长度不足时按实际拷贝，buf 需 ≥ 32 字节） */
int sps30_get_serial(sps30_handle_t *handle, char *buf, size_t buf_len);

/**
 * @brief  等待数据就绪并读取一次测量值
 * @note   内部轮询 SPS30_READ_TIMEOUT_MS，超时返回 IOT_TIMEOUT
 */
int sps30_read(sps30_handle_t *handle, sps30_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* SPS30_H */
