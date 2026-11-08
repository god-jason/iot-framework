#ifndef EM4100_H
#define EM4100_H

/*
 * EM4100 125kHz 只读卡 Manchester 解码驱动。
 *
 * 提供两种用法：
 *   1) 纯解码：把已采样（或由 em4095 采集）的位流交给
 *      em4100_decode_manchester() / em4100_decode_frame()；
 *   2) GPIO 轮询采样：em4100_read() 直接从天线上取 128 个半位并解码。
 *
 * 注意：轮询采样的时间基准来自 driver_delay_us()，若系统时基精度不足
 * 或采样被中断打断，需要改用定时器捕获（driver_io 需补充捕获抽象）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t raw[5];     /* 40 位卡号，大端字节序 */
    uint64_t id;
    bool valid;
} em4100_tag_t;

typedef struct {
    hal_pin_t pin;
    uint32_t half_bit_us;
    uint32_t timeout_ms;
    bool initialized;
} em4100_handle_t;

int em4100_init(em4100_handle_t *handle, hal_pin_t pin, uint32_t half_bit_us);
int em4100_deinit(em4100_handle_t *handle);
int em4100_set_timing(em4100_handle_t *handle, uint32_t half_bit_us);

/* 从任意相位开始扫描，找到合法帧后输出卡号 */
int em4100_decode_manchester(const uint8_t *bits, size_t bit_count, em4100_tag_t *tag);
/* 直接解析 5 字节卡号（已由外部完成 Manchester/校验） */
int em4100_decode_frame(const uint8_t frame[5], em4100_tag_t *tag);
/* GPIO 采样 + 解码；无卡或校验失败返回负值 */
int em4100_read(em4100_handle_t *handle, em4100_tag_t *tag, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* EM4100_H */
