#ifndef WM8978_H
#define WM8978_H

/*
 * Wolfson WM8978 音频编解码器驱动（I2C 控制 + I2S 数据）。
 *
 * 参考 libdriver 风格：
 *   wm8978_init / wm8978_deinit / wm8978_write / wm8978_read
 *   wm8978_play_start / wm8978_play_stop / wm8978_set_volume
 *
 * 说明：
 *   - 控制接口的每一帧为 16 位（7 位地址 + 9 位数据），线上 2 字节大端。
 *   - WM8978 控制口只写不可读，wm8978_read 返回驱动内部的影子寄存器值。
 *   - PCM 数据由调用方通过 port_i2s 提供，本驱动只负责打开/关闭芯片内
 *     的 DAC 与输出通路（播放基础路径）。
 *   - 具体寄存器组合（尤其是采样率、PLL、混音）需按 datasheet/固件补充确认。
 */

#include "common/driver_io.h"
#include "wm8978_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    driver_i2c_t i2c;
    uint16_t shadow[WM8978_REG_COUNT];
    bool playing;
    bool initialized;
} wm8978_handle_t;

/**
 * @brief  初始化 WM8978：probe、关闭输出与 DAC、设置 I2S 16bit 格式
 * @param  i2c      已初始化的 I2C 主机句柄
 * @param  address  7 位地址，0 表示默认 0x1A
 */
int wm8978_init(wm8978_handle_t *handle, port_i2c_handle_t *i2c, uint16_t address);

int wm8978_deinit(wm8978_handle_t *handle);

/** 写寄存器（9 位数据，自动打包为 2 字节） */
int wm8978_write(wm8978_handle_t *handle, uint8_t reg, uint16_t value);

/** 读影子寄存器（控制口不可读，返回驱动缓存值） */
int wm8978_read(wm8978_handle_t *handle, uint8_t reg, uint16_t *value);

/** 打开播放通路：输出混音 + DAC + 耳机/扬声器输出 */
int wm8978_play_start(wm8978_handle_t *handle);

/** 关闭播放通路（进入低功耗） */
int wm8978_play_stop(wm8978_handle_t *handle);

/** 设置输出音量：volume 为 0~63 的衰减码（0 = 0dB，每级约 1dB） */
int wm8978_set_volume(wm8978_handle_t *handle, uint8_t volume);

/** 静音控制 */
int wm8978_set_mute(wm8978_handle_t *handle, bool mute);

/** 选择扬声器输出（true）或耳机输出（false） */
int wm8978_set_speaker(wm8978_handle_t *handle, bool enable);

/** 设置音频接口格式与字长（从机模式） */
int wm8978_set_interface(wm8978_handle_t *handle, bool i2s, uint8_t word_length);

/** 设置采样率（8k/12k/16k/24k/32k/44.1k/48k/96k） */
int wm8978_set_sample_rate(wm8978_handle_t *handle, uint32_t sample_rate);

#ifdef __cplusplus
}
#endif

#endif /* WM8978_H */
