#ifndef NRF905_H
#define NRF905_H

/*
 * nRF905 433/868/915MHz 收发器驱动（SPI + TRX_CE/TX_EN/PWR_UP/DR 引脚）。
 *
 * 基础收发路径：ShockBurst 固定 32 字节 payload、固定地址、轮询 DR 引脚。
 * 未实现：CRC/地址宽度/输出功率等 RF-CONFIG 位域（需按 datasheet 配置，
 * 通过 nrf905_set_config() 写入调用方给出的 10 字节配置区）。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NRF905_MODE_POWER_DOWN = 0,
    NRF905_MODE_STANDBY,
    NRF905_MODE_RX,
    NRF905_MODE_TX
} nrf905_mode_t;

typedef struct {
    driver_spi_t spi;
    port_pin_t trx_ce;
    port_pin_t tx_en;
    port_pin_t pwr_up;
    port_pin_t dr;              /* 可传 0，则发送/接收按超时轮询 */
    uint32_t frequency_khz;
    uint8_t channel;            /* CH_NO */
    uint8_t config[10];         /* RF-CONFIG 区映像 */
    uint8_t address[4];         /* 收发共用地址 */
    uint32_t timeout_ms;
    bool initialized;
} nrf905_handle_t;

int nrf905_init(nrf905_handle_t *handle, port_spi_handle_t *spi, port_pin_t cs,
                port_pin_t trx_ce, port_pin_t tx_en, port_pin_t pwr_up, port_pin_t dr);
int nrf905_deinit(nrf905_handle_t *handle);

/* 频率单位 kHz：422400..473500 或 862000..932000 */
int nrf905_set_frequency(nrf905_handle_t *handle, uint32_t frequency_khz);
int nrf905_set_mode(nrf905_handle_t *handle, nrf905_mode_t mode);
int nrf905_set_address(nrf905_handle_t *handle, const uint8_t address[4]);
/* 高级接口：整体写入 10 字节 RF-CONFIG 区（含功率/AFW 等位域） */
int nrf905_set_config(nrf905_handle_t *handle, const uint8_t config[10]);

/* 发送 len(<=32) 字节，不足部分补 0 */
int nrf905_send(nrf905_handle_t *handle, const uint8_t *data, size_t len);
/* 非阻塞读：无数据返回 PORT_NOT_READY */
int nrf905_poll(nrf905_handle_t *handle, uint8_t *data, size_t len, size_t *received);
int nrf905_recv(nrf905_handle_t *handle, uint8_t *data, size_t len,
                size_t *received, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* NRF905_H */
