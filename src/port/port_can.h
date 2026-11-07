#ifndef PORT_CAN_H
#define PORT_CAN_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_CAN_MODE_NORMAL = 0,
    PORT_CAN_MODE_LOOPBACK,
    PORT_CAN_MODE_SILENT,
    PORT_CAN_MODE_SILENT_LOOPBACK
} port_can_mode_t;

typedef enum {
    PORT_CAN_ID_STANDARD = 0,
    PORT_CAN_ID_EXTENDED
} port_can_id_type_t;

typedef enum {
    PORT_CAN_FRAME_DATA = 0,
    PORT_CAN_FRAME_REMOTE
} port_can_frame_type_t;

typedef struct {
    uint32_t id;

    port_can_id_type_t id_type;
    port_can_frame_type_t frame_type;

    uint8_t dlc;
    uint8_t data[64];

    bool fd;
    bool brs;
} port_can_frame_t;

typedef struct {
    uint32_t bitrate;
    uint32_t data_bitrate;

    port_can_mode_t mode;
    bool enable_fd;
} port_can_config_t;

typedef struct {
    uint32_t id;
    uint32_t mask;
    port_can_id_type_t id_type;
} port_can_filter_t;

typedef struct port_can_handle {
    port_handle_base_t base;
    port_can_config_t config;

    port_callback_t callback;
    void *user_data;
} port_can_handle_t;

port_status_t port_can_init(port_can_handle_t *handle, const port_can_config_t *config);
port_status_t port_can_deinit(port_can_handle_t *handle);

port_status_t port_can_start(port_can_handle_t *handle);
port_status_t port_can_stop(port_can_handle_t *handle);

port_status_t port_can_transmit(port_can_handle_t *handle, const port_can_frame_t *frame,
                                uint32_t timeout);
port_status_t port_can_receive(port_can_handle_t *handle, port_can_frame_t *frame,
                               uint32_t timeout);
port_status_t port_can_transmit_it(port_can_handle_t *handle, const port_can_frame_t *frame);

port_status_t port_can_add_filter(port_can_handle_t *handle, const port_can_filter_t *filter);
port_status_t port_can_clear_filters(port_can_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_CAN_H */
