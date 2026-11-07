#ifndef SX1262_H
#define SX1262_H

/*
 * SX1262 LoRa 收发器驱动（SPI + BUSY/RESET/DIO1）。
 *
 * 本文件同时提供 SX1268 / LLCC68 的共用实现入口：
 * 两款器件的命令集与 SX1262 基本一致，仅频率范围与校准参数不同，
 * 由 sx1262_variant_t 区分（见 sx1262_init_variant）。
 *
 * 仅实现 LoRa 显式包头 + CRC 的点对点收发；GFSK、CAD、DIO 映射等
 * 需按 datasheet 补充。
 */

#include "common/driver_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SX1262_VARIANT_SX1262 = 0,
    SX1262_VARIANT_SX1268,
    SX1262_VARIANT_LLCC68
} sx1262_variant_t;

typedef enum {
    SX1262_MODE_SLEEP = 0,
    SX1262_MODE_STANDBY,
    SX1262_MODE_TX,
    SX1262_MODE_RX
} sx1262_mode_t;

typedef struct {
    driver_spi_t spi;
    iot_pin_t busy;
    iot_pin_t reset;
    iot_pin_t dio1;            /* 可传 0，则仅轮询 IRQ 状态寄存器 */
    uint32_t frequency_hz;
    uint8_t spreading_factor;   /* 5..12 */
    uint32_t bandwidth_hz;      /* 125000/250000/500000 */
    uint8_t coding_rate;        /* 1..4 -> 4/5..4/8 */
    int8_t tx_power_dbm;
    uint16_t preamble_len;
    sx1262_variant_t variant;
    uint32_t timeout_ms;
    bool initialized;
} sx1262_handle_t;

int sx1262_init(sx1262_handle_t *handle, iot_spi_t *spi, iot_pin_t cs,
                iot_pin_t busy, iot_pin_t reset, iot_pin_t dio1);
/* SX1268 / LLCC68 共用入口 */
int sx1262_init_variant(sx1262_handle_t *handle, iot_spi_t *spi, iot_pin_t cs,
                        iot_pin_t busy, iot_pin_t reset, iot_pin_t dio1,
                        sx1262_variant_t variant);
int sx1262_deinit(sx1262_handle_t *handle);

int sx1262_set_frequency(sx1262_handle_t *handle, uint32_t frequency_hz);
int sx1262_set_mode(sx1262_handle_t *handle, sx1262_mode_t mode);
/* 发射功率在 init 中按 tx_power_dbm 字段配置（默认 14dBm） */
int sx1262_set_modulation(sx1262_handle_t *handle, uint8_t spreading_factor,
                          uint32_t bandwidth_hz, uint8_t coding_rate);

/* 发送一帧（LoRa 显式包头，payload 1..255 字节） */
int sx1262_send(sx1262_handle_t *handle, const uint8_t *data, size_t len);
/* 接收：无数据返回 IOT_NOT_READY，需先 set_mode(RX) 进入接收 */
int sx1262_poll(sx1262_handle_t *handle, uint8_t *data, size_t len, size_t *received);

#ifdef __cplusplus
}
#endif

#endif /* SX1262_H */
