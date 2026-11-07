#ifndef AS608_H
#define AS608_H

/*
 * AS608 指纹模块驱动（UART 协议：0xEF01 包头 + 指令包 + 应答包）。
 *
 * 说明：drivers/common/driver_io.h 目前没有 UART 抽象，本驱动直接调用
 * port_uart_transmit()/port_uart_receive()（同属 src/port 抽象层，
 * 不依赖任何 HAL）；后续如补充 driver_uart_* 可无痛替换。
 *
 * 已实现：指令包收发、密码校验、取图像、生成特征、搜索、读模板数量。
 * 未实现：模板上传/下载、LED 控制等（按需按协议扩展）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    port_uart_handle_t *uart;
    uint32_t address;
    uint32_t password;
    uint32_t timeout_ms;
    bool initialized;
} as608_handle_t;

typedef struct {
    uint8_t confirm;                        /* 确认码，0 表示成功 */
    uint8_t data[64];                       /* 参数区 */
    size_t len;
} as608_response_t;

int as608_init(as608_handle_t *handle, port_uart_handle_t *uart, uint32_t password);
int as608_deinit(as608_handle_t *handle);

/* 发送指令包（cmd + 可选参数） */
int as608_write(as608_handle_t *handle, uint8_t cmd, const uint8_t *params, size_t len);
/* 接收并校验应答包 */
int as608_read(as608_handle_t *handle, as608_response_t *response);

int as608_verify_password(as608_handle_t *handle);
int as608_gen_image(as608_handle_t *handle);
/* buf_id: 1 或 2 */
int as608_image2tz(as608_handle_t *handle, uint8_t buf_id);
/* 搜索指纹库，输出匹配页码与得分 */
int as608_search(as608_handle_t *handle, uint8_t buf_id,
                 uint16_t *page, uint16_t *score);
int as608_get_count(as608_handle_t *handle, uint16_t *count);

#ifdef __cplusplus
}
#endif

#endif /* AS608_H */
