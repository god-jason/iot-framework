#ifndef PORT_FLASH_H
#define PORT_FLASH_H

#include "port_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_FLASH_FLAG_READ = PORT_BIT(0),
    PORT_FLASH_FLAG_WRITE = PORT_BIT(1),
    PORT_FLASH_FLAG_ERASE = PORT_BIT(2)
} port_flash_flags_t;

typedef struct {
    uintptr_t start;
    size_t size;

    size_t erase_size;
    size_t program_size;

    uint32_t flags;
} port_flash_info_t;

typedef struct port_flash_handle {
    port_handle_base_t base;
    port_flash_info_t info;
} port_flash_handle_t;

port_status_t port_flash_init(port_flash_handle_t *handle);
port_status_t port_flash_deinit(port_flash_handle_t *handle);

port_status_t port_flash_get_info(port_flash_handle_t *handle, port_flash_info_t *info);

port_status_t port_flash_read(port_flash_handle_t *handle, uintptr_t address,
                              void *buffer, size_t length);
port_status_t port_flash_write(port_flash_handle_t *handle, uintptr_t address,
                               const void *buffer, size_t length);
port_status_t port_flash_erase(port_flash_handle_t *handle, uintptr_t address, size_t length);

port_status_t port_flash_lock(port_flash_handle_t *handle);
port_status_t port_flash_unlock(port_flash_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* PORT_FLASH_H */
