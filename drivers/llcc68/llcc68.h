#ifndef LLCC68_H
#define LLCC68_H

/*
 * LLCC68 LoRa 收发器（150..960MHz）。
 * 命令集与 SX1262 基本一致，共用 ../sx1262/sx1262.c 的实现；
 * 若器件在特定频段需要不同的镜像校准/PA 配置，需按 datasheet 补充。
 */

#include "../sx1262/sx1262.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef sx1262_handle_t llcc68_handle_t;
typedef sx1262_mode_e llcc68_mode_t;

int llcc68_init(llcc68_handle_t *handle, hal_spi_t *spi, hal_pin_t cs,
                hal_pin_t busy, hal_pin_t reset, hal_pin_t dio1);
int llcc68_deinit(llcc68_handle_t *handle);
int llcc68_set_frequency(llcc68_handle_t *handle, uint32_t frequency_hz);
int llcc68_set_mode(llcc68_handle_t *handle, llcc68_mode_t mode);
int llcc68_set_modulation(llcc68_handle_t *handle, uint8_t spreading_factor,
                          uint32_t bandwidth_hz, uint8_t coding_rate);
int llcc68_send(llcc68_handle_t *handle, const uint8_t *data, size_t len);
int llcc68_poll(llcc68_handle_t *handle, uint8_t *data, size_t len, size_t *received);

#ifdef __cplusplus
}
#endif

#endif /* LLCC68_H */
