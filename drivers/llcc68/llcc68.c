#include "llcc68.h"

/* LLCC68 与 SX1262 共用实现，仅 variant 不同（PA/校准差异需按 datasheet 复核） */

int llcc68_init(llcc68_handle_t *handle, hal_spi_t *spi, hal_pin_t cs,
                hal_pin_t busy, hal_pin_t reset, hal_pin_t dio1)
{
    return sx1262_init_variant(handle, spi, cs, busy, reset, dio1, SX1262_VARIANT_LLCC68);
}

int llcc68_deinit(llcc68_handle_t *handle)
{
    return sx1262_deinit(handle);
}

int llcc68_set_frequency(llcc68_handle_t *handle, uint32_t frequency_hz)
{
    return sx1262_set_frequency(handle, frequency_hz);
}

int llcc68_set_mode(llcc68_handle_t *handle, llcc68_mode_t mode)
{
    return sx1262_set_mode(handle, mode);
}

int llcc68_set_modulation(llcc68_handle_t *handle, uint8_t spreading_factor,
                          uint32_t bandwidth_hz, uint8_t coding_rate)
{
    return sx1262_set_modulation(handle, spreading_factor, bandwidth_hz, coding_rate);
}

int llcc68_send(llcc68_handle_t *handle, const uint8_t *data, size_t len)
{
    return sx1262_send(handle, data, len);
}

int llcc68_poll(llcc68_handle_t *handle, uint8_t *data, size_t len, size_t *received)
{
    return sx1262_poll(handle, data, len, received);
}
