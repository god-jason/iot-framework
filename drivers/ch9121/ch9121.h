#ifndef CH9121_H
#define CH9121_H

/*
 * CH9121 串口转以太网（UART / SPI 配置，串口透传数据）。
 *
 * 参考 libdriver 风格：
 *   ch9121_init / ch9121_deinit / ch9121_read / ch9121_write
 *   ch9121_set_ip / ch9121_set_local_port / ch9121_set_mode ...
 *
 * 说明：
 *   - 数据透传与参数配置共用同一 UART（CH9121 的配置脚 CFG0/1 需置为配置模式，
 *     由硬件/板级完成）。UART 句柄由调用方初始化和持有。
 *   - 配置帧收发为“命令 + 可选响应”，响应解析仅做基本帧头与校验检查；
 *     具体命令码与字段顺序需按 datasheet/固件补充。
 *   - SPI 配置通道本实现未启用，如需要由调用方使用 iot_spi 实现，
 *     或在后续版本补充（见 ch9121_write 注释）。
 */

#include "common/driver_io.h"
#include "ch9121_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CH9121_NET_TCP_SERVER = 0,
    CH9121_NET_TCP_CLIENT,
    CH9121_NET_UDP_SERVER,
    CH9121_NET_UDP_CLIENT
} ch9121_net_mode_t;

typedef struct {
    iot_uart_t *uart;
    uint32_t timeout_ms;
    ch9121_net_mode_t mode;
    bool initialized;
} ch9121_handle_t;

/**
 * @brief  初始化 CH9121 驱动（只保存 UART 句柄，不改变芯片配置）
 * @param  uart  已初始化的 UART 句柄（波特率需与芯片一致）
 */
int ch9121_init(ch9121_handle_t *handle, iot_uart_t *uart, uint32_t timeout_ms);

int ch9121_deinit(ch9121_handle_t *handle);

/** 透传发送数据到网络 */
int ch9121_write(ch9121_handle_t *handle, const uint8_t *data, size_t len);

/** 从串口读取透传数据（超时时间由调用方给出） */
int ch9121_read(ch9121_handle_t *handle, uint8_t *data, size_t len, uint32_t timeout_ms);

/** 发送一条原始配置帧 */
int ch9121_send_command(ch9121_handle_t *handle, uint8_t cmd,
                        const uint8_t *data, size_t len);

/** 读取配置回应帧，out_len 出参为实际数据长度（不含帧头/校验） */
int ch9121_read_response(ch9121_handle_t *handle, uint8_t *cmd, uint8_t *data,
                         size_t capacity, size_t *out_len);

int ch9121_set_ip(ch9121_handle_t *handle, const uint8_t ip[4]);
int ch9121_set_subnet(ch9121_handle_t *handle, const uint8_t mask[4]);
int ch9121_set_gateway(ch9121_handle_t *handle, const uint8_t gateway[4]);
int ch9121_set_local_port(ch9121_handle_t *handle, uint16_t port);
int ch9121_set_dest_ip(ch9121_handle_t *handle, const uint8_t ip[4]);
int ch9121_set_dest_port(ch9121_handle_t *handle, uint16_t port);
int ch9121_set_mode(ch9121_handle_t *handle, ch9121_net_mode_t mode);
int ch9121_set_baudrate(ch9121_handle_t *handle, uint32_t baudrate);

/** 保存参数并重启芯片 */
int ch9121_save_reset(ch9121_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* CH9121_H */
