#ifndef CH9120_H
#define CH9120_H

/*
 * CH9120 串口转以太网驱动（UART 配置 + 透传）。
 *
 * 参考 libdriver 风格：
 *   ch9120_init / ch9120_deinit / ch9120_read / ch9120_write
 *   ch9120_set_ip / ch9120_set_local_port / ch9120_set_mode ...
 *
 * 说明：
 *   - UART 句柄由调用方创建并配置波特率，驱动只负责收发与参数帧封装。
 *   - 配置命令的字节序与长度需按 datasheet/固件补充确认；
 *     本实现提供基础框架，未知命令一律通过 ch9120_send_command 透传。
 */

#include "common/driver_io.h"
#include "ch9120_register.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CH9120_NET_TCP_SERVER = 0,
    CH9120_NET_TCP_CLIENT,
    CH9120_NET_UDP_SERVER,
    CH9120_NET_UDP_CLIENT
} ch9120_net_mode_e;

typedef struct {
    hal_uart_t *uart;
    uint32_t timeout_ms;
    ch9120_net_mode_e mode;
    bool initialized;
} ch9120_handle_t;

int ch9120_init(ch9120_handle_t *handle, hal_uart_t *uart, uint32_t timeout_ms);

int ch9120_deinit(ch9120_handle_t *handle);

/** 透传发送 */
int ch9120_write(ch9120_handle_t *handle, const uint8_t *data, size_t len);

/** 透传接收 */
int ch9120_read(ch9120_handle_t *handle, uint8_t *data, size_t len, uint32_t timeout_ms);

/** 发送原始配置帧 */
int ch9120_send_command(ch9120_handle_t *handle, uint8_t cmd,
                        const uint8_t *data, size_t len);

/** 读取配置回应帧 */
int ch9120_read_response(ch9120_handle_t *handle, uint8_t *cmd, uint8_t *data,
                         size_t capacity, size_t *out_len);

int ch9120_set_ip(ch9120_handle_t *handle, const uint8_t ip[4]);
int ch9120_set_subnet(ch9120_handle_t *handle, const uint8_t mask[4]);
int ch9120_set_gateway(ch9120_handle_t *handle, const uint8_t gateway[4]);
int ch9120_set_local_port(ch9120_handle_t *handle, uint16_t port);
int ch9120_set_dest_ip(ch9120_handle_t *handle, const uint8_t ip[4]);
int ch9120_set_dest_port(ch9120_handle_t *handle, uint16_t port);
int ch9120_set_mode(ch9120_handle_t *handle, ch9120_net_mode_e mode);
int ch9120_set_baudrate(ch9120_handle_t *handle, uint32_t baudrate);

int ch9120_save_reset(ch9120_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* CH9120_H */
