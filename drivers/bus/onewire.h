#ifndef ONEWIRE_H
#define ONEWIRE_H

/* 单总线（1-Wire）总线：用于 DS18B20 等。按标准时序 bit-bang。 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*set_level)(void *ctx, uint8_t level);
    uint8_t (*get_level)(void *ctx);
    void (*udelay)(void *ctx, uint32_t us);
    void *ctx;
} onewire_bus_t;

int onewire_reset(const onewire_bus_t *bus);
void onewire_write_byte(const onewire_bus_t *bus, uint8_t b);
uint8_t onewire_read_byte(const onewire_bus_t *bus);

#ifdef __cplusplus
}
#endif

#endif /* ONEWIRE_H */