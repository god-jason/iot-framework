#include "sx1268.h"

/* SX1268 与 SX1262 共用实现，仅 variant 不同（频段 410..810MHz） */

int sx1268_init(sx1268_handle_t *handle, port_spi_handle_t *spi, port_pin_t cs,
                port_pin_t busy, port_pin_t reset, port_pin_t dio1)
{
    return sx1262_init_variant(handle, spi, cs, busy, reset, dio1, SX1262_VARIANT_SX1268);
}

int sx1268_deinit(sx1268_handle_t *handle)
{
    return sx1262_deinit(handle);
}

int sx1268_set_frequency(sx1268_handle_t *handle, uint32_t frequency_hz)
{
    return sx1262_set_frequency(handle, frequency_hz);
}

int sx1268_set_mode(sx1268_handle_t *handle, sx1268_mode_t mode)
{
    return sx1262_set_mode(handle, mode);
}

int sx1268_set_modulation(sx1268_handle_t *handle, uint8_t spreading_factor,
                          uint32_t bandwidth_hz, uint8_t coding_rate)
{
    return sx1262_set_modulation(handle, spreading_factor, bandwidth_hz, coding_rate);
}

int sx1268_send(sx1268_handle_t *handle, const uint8_t *data, size_t len)
{
    return sx1262_send(handle, data, len);
}

int sx1268_poll(sx1268_handle_t *handle, uint8_t *data, size_t len, size_t *received)
{
    return sx1262_poll(handle, data, len, received);
}
