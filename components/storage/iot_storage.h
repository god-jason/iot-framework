#ifndef IOT_STORAGE_H
#define IOT_STORAGE_H

/*
 * 存储后端注册表：按名管理多个存储介质（内部 Flash 区 / 外部 SPI Flash / EEPROM）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*init)(void *ctx);
    int (*read)(void *ctx, uint32_t addr, void *data, uint32_t len);
    int (*write)(void *ctx, uint32_t addr, const void *data, uint32_t len);
    int (*erase)(void *ctx, uint32_t addr, uint32_t len);
    void *ctx;
} iot_storage_ops_t;

int iot_storage_register(const char *name, const iot_storage_ops_t *ops);
int iot_storage_unregister(const char *name);

int iot_storage_read(const char *name, uint32_t addr, void *data, uint32_t len);
int iot_storage_write(const char *name, uint32_t addr, const void *data, uint32_t len);
int iot_storage_erase(const char *name, uint32_t addr, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* IOT_STORAGE_H */