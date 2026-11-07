#ifndef TEA5767_H
#define TEA5767_H

/*
 * NXP TEA5767 FM 收音机驱动（I2C，5 字节写 / 5 字节读）。
 *
 * 参考 libdriver 风格：
 *   tea5767_init / tea5767_deinit / tea5767_read / tea5767_write
 * 频率换算：PLL = 4 * (f_Hz + 225000) / 32768；
 *           f_Hz = PLL * 32768 / 4 - 225000。
 * 音频输出由芯片直接驱动耳机，无需上层 PCM 通路。
 */

#include "common/driver_io.h"
#include "tea5767_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool ready;          /* READY 位：1 表示已调谐完成 */
    bool stereo;         /* 立体声导频检测 */
    bool band_japan;     /* 当前频段 */
    uint16_t pll;        /* 原始 14 位 PLL 值 */
    float frequency_mhz; /* 换算后的频率 */
    uint8_t if_count;    /* IF 计数器（越接近 0x2E 表示信号越好） */
    uint8_t signal_level;/* 信号电平 ADC 值 */
} tea5767_status_t;

typedef struct {
    driver_i2c_t i2c;
    uint8_t config[TEA5767_REG_COUNT]; /* 本地缓存的 5 字节写配置 */
    bool initialized;
} tea5767_handle_t;

/**
 * @brief  初始化 TEA5767
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址，0 表示默认 0x60
 */
int tea5767_init(tea5767_handle_t *handle, iot_i2c_t *i2c, uint16_t address);

int tea5767_deinit(tea5767_handle_t *handle);

/** 读取并解析芯片状态 */
int tea5767_read(tea5767_handle_t *handle, tea5767_status_t *status);

/** 直接写入 5 字节配置（同时更新本地缓存） */
int tea5767_write(tea5767_handle_t *handle, const uint8_t config[TEA5767_REG_COUNT]);

/** 设置频率（MHz），失败返回 IOT_INVALID_PARAM */
int tea5767_set_frequency(tea5767_handle_t *handle, float frequency_mhz);

/** 从状态寄存器换算当前频率 */
int tea5767_get_frequency(tea5767_handle_t *handle, float *frequency_mhz);

/** 静音控制 */
int tea5767_set_mute(tea5767_handle_t *handle, bool mute);

/** 单声道 / 立体声切换（enable 为 true 表示立体声） */
int tea5767_set_stereo(tea5767_handle_t *handle, bool enable);

/** 待机控制 */
int tea5767_set_standby(tea5767_handle_t *handle, bool standby);

/** 自动搜索，up 表示向上扫描；找到电台后 mhz_out 返回频率 */
int tea5767_seek(tea5767_handle_t *handle, bool up, float *mhz_out);

#ifdef __cplusplus
}
#endif

#endif /* TEA5767_H */
