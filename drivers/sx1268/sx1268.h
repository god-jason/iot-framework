#ifndef SX1268_H
#define SX1268_H

/*
 * SX1268 LoRa 收发器（410..810MHz）。
 * 与 SX1262 共用实现（命令集一致，仅频率范围/镜像校准不同），
 * 见 ../sx1262/sx1262.c 中的 sx1262_init_variant()。
 */

#include "../sx1262/sx1262.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef sx1262_handle_t sx1268_handle_t;
typedef sx1262_mode_e sx1268_mode_t;

int sx1268_init(sx1268_handle_t *handle, hal_spi_t *spi, hal_pin_t cs,
                hal_pin_t busy, hal_pin_t reset, hal_pin_t dio1);
int sx1268_deinit(sx1268_handle_t *handle);
int sx1268_set_frequency(sx1268_handle_t *handle, uint32_t frequency_hz);
int sx1268_set_mode(sx1268_handle_t *handle, sx1268_mode_t mode);
int sx1268_set_modulation(sx1268_handle_t *handle, uint8_t spreading_factor,
                          uint32_t bandwidth_hz, uint8_t coding_rate);
int sx1268_send(sx1268_handle_t *handle, const uint8_t *data, size_t len);
int sx1268_poll(sx1268_handle_t *handle, uint8_t *data, size_t len, size_t *received);

#ifdef __cplusplus
}
#endif

#endif /* SX1268_H */
