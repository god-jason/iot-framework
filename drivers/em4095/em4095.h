#ifndef EM4095_H
#define EM4095_H

/*
 * EM4095 125kHz RFID 读头前端（SHD/MOD/RDY_CLK/DOUT GPIO）。
 *
 * EM4095 只完成模拟解调，输出 Manchester 编码的 DOUT 与 RDY_CLK 位时钟；
 * 帧解析交给 em4100 驱动。DOUT 采样相位与 RDY_CLK 频率相关，
 * 具体延时需按实际天线/时钟标定（见 em4095_register.h）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_pin_t shd;         /* 高电平关断 */
    hal_pin_t mod;         /* 场调制输出 */
    hal_pin_t rdy_clk;     /* 位时钟输入 */
    hal_pin_t dout;        /* 解调数据输入 */
    uint32_t timeout_ms;
    bool initialized;
} em4095_handle_t;

int em4095_init(em4095_handle_t *handle, hal_pin_t shd, hal_pin_t mod,
                hal_pin_t rdy_clk, hal_pin_t dout);
int em4095_deinit(em4095_handle_t *handle);

/* 使能/关断读头（SHD 引脚） */
int em4095_set_power(em4095_handle_t *handle, bool on);
/* 控制标签场调制（MOD 引脚），用于向卡写数据 */
int em4095_set_modulation(em4095_handle_t *handle, bool on);

/* 按 RDY_CLK 逐位采样 DOUT，bits 中每字节存放 1 个位（0/1） */
int em4095_read(em4095_handle_t *handle, uint8_t *bits, size_t max_bits,
                size_t *bit_count, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* EM4095_H */
