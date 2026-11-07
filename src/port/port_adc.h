#ifndef PORT_ADC_H
#define PORT_ADC_H

#include "port_def.h"
#include "port_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_ADC_RESOLUTION_6BIT = 6,
    PORT_ADC_RESOLUTION_8BIT = 8,
    PORT_ADC_RESOLUTION_10BIT = 10,
    PORT_ADC_RESOLUTION_12BIT = 12,
    PORT_ADC_RESOLUTION_14BIT = 14,
    PORT_ADC_RESOLUTION_16BIT = 16
} port_adc_resolution_t;

typedef enum {
    PORT_ADC_ALIGN_RIGHT = 0,
    PORT_ADC_ALIGN_LEFT
} port_adc_align_t;

typedef enum {
    PORT_ADC_TRIGGER_SOFTWARE = 0,
    PORT_ADC_TRIGGER_TIMER,
    PORT_ADC_TRIGGER_EXTERNAL
} port_adc_trigger_t;

typedef struct {
    port_adc_resolution_t resolution;
    port_adc_align_t alignment;
    port_adc_trigger_t trigger;

    uint8_t channel_count;
} port_adc_config_t;

typedef struct {
    uint8_t channel;
    uint8_t rank;
    uint32_t sample_time;
} port_adc_channel_config_t;

typedef struct port_adc_handle {
    port_handle_base_t base;

    port_adc_config_t config;
    port_dma_handle_t *dma;

    port_callback_t callback;
    void *user_data;
} port_adc_handle_t;

port_status_t port_adc_init(port_adc_handle_t *handle, const port_adc_config_t *config);
port_status_t port_adc_deinit(port_adc_handle_t *handle);

port_status_t port_adc_config_channel(port_adc_handle_t *handle,
                                      const port_adc_channel_config_t *config);

port_status_t port_adc_start(port_adc_handle_t *handle);
port_status_t port_adc_stop(port_adc_handle_t *handle);

port_status_t port_adc_read(port_adc_handle_t *handle, uint8_t channel,
                            uint32_t *value, uint32_t timeout);

port_status_t port_adc_start_it(port_adc_handle_t *handle);
port_status_t port_adc_start_dma(port_adc_handle_t *handle, uint32_t *buffer, size_t length);

port_status_t port_adc_calibrate(port_adc_handle_t *handle);

uint32_t port_adc_get_max_value(port_adc_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ADC_H */
