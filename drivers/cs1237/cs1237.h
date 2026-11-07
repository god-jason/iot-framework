#ifndef CS1237_H
#define CS1237_H

/*
 * CS1237 24 位 ADC 驱动（2 线 GPIO：SCLK + DRDY/DOUT，软件时序）。
 *
 * 参考 libdriver 风格：cs1237_init / cs1237_deinit / cs1237_read ...
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 增益档位 */
typedef enum {
    CS1237_GAIN_1 = 0,
    CS1237_GAIN_2,
    CS1237_GAIN_64,
    CS1237_GAIN_128
} cs1237_gain_t;

/* 输出速率档位 */
typedef enum {
    CS1237_RATE_10HZ = 0,
    CS1237_RATE_40HZ,
    CS1237_RATE_640HZ,
    CS1237_RATE_1280HZ
} cs1237_rate_t;

typedef struct {
    iot_pin_t sclk;        /* 时钟，主机输出 */
    iot_pin_t dout;        /* DRDY/DOUT，芯片输出（低电平=就绪） */
    cs1237_gain_t gain;     /* 当前增益档位 */
    cs1237_rate_t rate;     /* 当前输出速率档位 */
    int32_t offset;         /* 零点（皮重）原始值 */
    float scale;            /* 换算系数：重量 = (raw - offset) / scale */
    bool initialized;
} cs1237_handle_t;

/**
 * @brief  初始化（SCLK 输出低电平，DOUT 输入）
 * @param  handle  驱动句柄
 * @param  sclk    SCLK 引脚
 * @param  dout    DRDY/DOUT 引脚
 * @return 0 成功，负值为 iot_status_t
 */
int cs1237_init(cs1237_handle_t *handle, iot_pin_t sclk, iot_pin_t dout);

/** @brief 反初始化，释放引脚 */
int cs1237_deinit(cs1237_handle_t *handle);

/**
 * @brief  查询是否有新的转换结果（不消耗数据）
 * @return 1 就绪，0 忙，负值为错误码
 */
int cs1237_is_ready(cs1237_handle_t *handle);

/**
 * @brief  读取一次 24 位原始值（阻塞等待就绪）
 * @param  raw  24 位补码扩展后的有符号值
 */
int cs1237_read_raw(cs1237_handle_t *handle, int32_t *raw);

/** @brief 连续读取 times 次并取平均 */
int cs1237_read_average(cs1237_handle_t *handle, uint8_t times, int32_t *raw);

/** @brief 设置增益档位（仅记录，需调用 cs1237_write_config 生效） */
int cs1237_set_gain(cs1237_handle_t *handle, cs1237_gain_t gain);

/** @brief 设置输出速率档位（仅记录，需调用 cs1237_write_config 生效） */
int cs1237_set_rate(cs1237_handle_t *handle, cs1237_rate_t rate);

/**
 * @brief  写配置寄存器（增益与速率）
 * @note   配置帧的时钟格式需按 datasheet 补充确认，当前为基础实现。
 */
int cs1237_write_config(cs1237_handle_t *handle);

/** @brief 设置零点原始值 */
int cs1237_set_offset(cs1237_handle_t *handle, int32_t offset);

/** @brief 设置换算系数（每单位重量对应的原始计数） */
int cs1237_set_scale(cs1237_handle_t *handle, float scale);

/** @brief 采集 times 次平均作为零点（皮重） */
int cs1237_tare(cs1237_handle_t *handle, uint8_t times);

/** @brief 读取换算后的重量 */
int cs1237_read(cs1237_handle_t *handle, float *weight);

#ifdef __cplusplus
}
#endif

#endif /* CS1237_H */
