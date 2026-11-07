#ifndef IOT_SETTINGS_H
#define IOT_SETTINGS_H

/*
 * 配置项（Settings）管理：参数注册（默认值）+ RAM 读写 + 重置。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    const void *default_value;
    uint16_t size;
} iot_settings_item_t;

int iot_settings_init(void);

int iot_settings_register(const iot_settings_item_t *item);

int iot_settings_get(const char *name, void *value, uint16_t size);
int iot_settings_set(const char *name, const void *value, uint16_t size);
int iot_settings_reset(const char *name);
int iot_settings_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_SETTINGS_H */