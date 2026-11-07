#ifndef PORT_USB_H
#define PORT_USB_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_USB_MODE_DEVICE = 0,
    PORT_USB_MODE_HOST
} port_usb_mode_t;

typedef enum {
    PORT_USB_SPEED_FULL = 0,
    PORT_USB_SPEED_HIGH,
    PORT_USB_SPEED_LOW
} port_usb_speed_t;

typedef struct {
    port_usb_mode_t mode;
    port_usb_speed_t speed;
} port_usb_config_t;

typedef struct port_usb_handle {
    port_handle_base_t base;
    port_usb_config_t config;
    port_callback_t callback;
    void *user_data;
} port_usb_handle_t;

port_status_t port_usb_init(port_usb_handle_t *handle, const port_usb_config_t *config);
port_status_t port_usb_deinit(port_usb_handle_t *handle);

port_status_t port_usb_start(port_usb_handle_t *handle);
port_status_t port_usb_stop(port_usb_handle_t *handle);

port_status_t port_usb_connect(port_usb_handle_t *handle);
port_status_t port_usb_disconnect(port_usb_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_USB_H */
