#ifndef IR_REMOTE_H
#define IR_REMOTE_H

/*
 * 红外收发驱动（NEC 编码/解码，GPIO + 软件载波/轮询采样）。
 *
 * 发送：TX 脚输出 38kHz 调制波形（软件 PWM，时长精度依赖 delay_us）。
 * 接收：RX 脚接一体化接收头（空闲高、收到载波为低），轮询采样记录
 *       各段 mark/space 时长，再由 ir_remote_decode_nec() 解析。
 *
 * 说明：HAL 层暂无输入捕获抽象，因此接收采用轮询计时；
 * 若需要高精度解码，应在 HAL 增加定时器捕获接口后替换采样部分。
 */

#include "hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t address;
    uint8_t command;
    bool address_inverted;   /* NEC 常规帧：第二字节为地址反码 */
} ir_remote_frame_t;

typedef struct {
    hal_pin_t tx_pin;          /* 可传 0（仅接收） */
    hal_pin_t rx_pin;          /* 可传 0（仅发送） */
    uint16_t carrier_khz;
    uint32_t timeout_ms;
    bool initialized;
} ir_remote_handle_t;

int ir_remote_init(ir_remote_handle_t *handle, hal_pin_t tx_pin, hal_pin_t rx_pin);
int ir_remote_deinit(ir_remote_handle_t *handle);
int ir_remote_set_carrier(ir_remote_handle_t *handle, uint16_t carrier_khz);

/* 发送 NEC 帧（地址 + 地址反码 + 命令 + 命令反码，LSB 先） */
int ir_remote_send_nec(ir_remote_handle_t *handle, uint8_t address, uint8_t command);

/* 编码为 mark/space 时长序列（us），便于测试与回放 */
int ir_remote_encode_nec(const ir_remote_frame_t *frame, uint16_t *timings,
                         size_t max_timings, size_t *count);
/* 解析 mark/space 时长序列 */
int ir_remote_decode_nec(const uint16_t *timings, size_t count, ir_remote_frame_t *frame);

/* 轮询接收一帧，timings 由调用方提供（建议 >= 68 项） */
int ir_remote_recv(ir_remote_handle_t *handle, uint16_t *timings, size_t max_timings,
                   size_t *count, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* IR_REMOTE_H */
