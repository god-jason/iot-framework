#ifndef NRF24L01_H
#define NRF24L01_H

/*
 * nRF24L01+ 2.4GHz 收发器驱动（SPI + CE/IRQ GPIO）。
 * 仅实现点对点基础收发：静态 payload 长度、固定地址，未实现
 * 动态 payload / ACK payload / 多管道特性（需按 datasheet 补充）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NRF24L01_MODE_POWER_DOWN = 0,   /* PWR_UP=0 */
    NRF24L01_MODE_STANDBY,          /* PWR_UP=1, CE=0, TX 方向 */
    NRF24L01_MODE_TX,               /* 同 STANDBY，send() 内拉高 CE */
    NRF24L01_MODE_RX                /* PWR_UP=1, PRIM_RX=1, CE=1 */
} nrf24l01_mode_e;

typedef struct {
    driver_spi_t spi;
    hal_pin_t ce;              /* 必需 */
    hal_pin_t irq;             /* 可传 0（不使用中断） */
    uint8_t channel;            /* 0..125 -> 2400..2525MHz */
    uint8_t payload_size;       /* 1..32 */
    uint8_t address_width;      /* 3..5 */
    uint32_t timeout_ms;
    bool initialized;
} nrf24l01_handle_t;

int nrf24l01_init(nrf24l01_handle_t *handle, hal_spi_t *spi,
                  hal_pin_t cs, hal_pin_t ce, hal_pin_t irq);
int nrf24l01_deinit(nrf24l01_handle_t *handle);

/* 频率单位 kHz，范围 2400000..2525000 */
int nrf24l01_set_frequency(nrf24l01_handle_t *handle, uint32_t frequency_khz);
int nrf24l01_set_mode(nrf24l01_handle_t *handle, nrf24l01_mode_e mode);
/* 地址同时写入 TX_ADDR 与 RX_ADDR_P0，收发共用；payload 固定 32 字节 */
int nrf24l01_set_address(nrf24l01_handle_t *handle, const uint8_t *address,
                         uint8_t width);

/* 发送一帧，len <= payload_size */
int nrf24l01_send(nrf24l01_handle_t *handle, const uint8_t *data, size_t len);
/* 非阻塞取一帧；无数据返回 HAL_NOT_READY，received 可为 NULL */
int nrf24l01_poll(nrf24l01_handle_t *handle, uint8_t *data, size_t len,
                  size_t *received);
/* 阻塞等待一帧 */
int nrf24l01_recv(nrf24l01_handle_t *handle, uint8_t *data, size_t len,
                  size_t *received, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* NRF24L01_H */
