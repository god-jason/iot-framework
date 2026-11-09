#ifndef WT5888E02_H
#define WT5888E02_H

/*
 * WT5888E02 语音播报模块驱动（UART 或一线串口控制）。
 *
 * 参考 libdriver 风格：
 *   wt5888e02_init / wt5888e02_deinit / wt5888e02_play / wt5888e02_stop
 *
 * 两种控制方式：
 *   - UART：调用方传入已初始化的 hal_uart_t（通常 9600 8N1）
 *   - 一线串口：只用一个 GPIO 数据脚，按时序发送脉宽编码
 * BUSY 脚可选，用于查询当前是否在播放。
 */

#include "common/driver_io.h"
#include "wt5888e02_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WT5888E02_BUS_UART = 0,
    WT5888E02_BUS_ONELINE
} wt5888e02_bus_e;

typedef struct {
    hal_uart_t *uart;   /* UART 模式必需，一线模式可为 NULL */
    hal_pin_t data_pin;        /* 一线串口数据脚，UART 模式为 0 */
    hal_pin_t busy_pin;        /* BUSY 指示脚，0 表示不检测 */
    wt5888e02_bus_e bus;
    uint32_t timeout_ms;
    bool initialized;
} wt5888e02_handle_t;

/**
 * @brief  以 UART 方式初始化
 * @param  uart     已初始化的 UART 句柄
 * @param  busy_pin BUSY 引脚，0 表示不接
 */
int wt5888e02_init(wt5888e02_handle_t *handle, hal_uart_t *uart,
                   hal_pin_t busy_pin);

/** 以一线串口方式初始化（data_pin 为双向数据脚） */
int wt5888e02_init_oneline(wt5888e02_handle_t *handle, hal_pin_t data_pin,
                           hal_pin_t busy_pin);

int wt5888e02_deinit(wt5888e02_handle_t *handle);

/** 发送原始控制帧（UART 模式为完整帧；一线模式为 3 字节：cmd + 2 data） */
int wt5888e02_write(wt5888e02_handle_t *handle, const uint8_t *frame, size_t len);

/** 播放指定曲目（0~65535） */
int wt5888e02_play(wt5888e02_handle_t *handle, uint16_t index);

/** 停止当前播放 */
int wt5888e02_stop(wt5888e02_handle_t *handle);

/** 设置音量 0~30 */
int wt5888e02_set_volume(wt5888e02_handle_t *handle, uint8_t volume);

/** 设置单次/循环播放 */
int wt5888e02_set_loop(wt5888e02_handle_t *handle, bool loop);

/** 查询 BUSY 状态；未接 BUSY 脚返回 HAL_NOT_SUPPORTED */
int wt5888e02_busy(wt5888e02_handle_t *handle, bool *busy);

#ifdef __cplusplus
}
#endif

#endif /* WT5888E02_H */
