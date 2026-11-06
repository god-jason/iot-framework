#ifndef IOT_UPGRADE_H
#define IOT_UPGRADE_H

/*
 * 升级管理器：接收固件 → 写入 OTA 分区 → 校验 → 完成/中止。
 * 与 Bootloader（iot_boot）分离：本模块负责下载/暂存/校验。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IOT_UPGRADE_IDLE,
    IOT_UPGRADE_PREPARE,
    IOT_UPGRADE_RECEIVING,
    IOT_UPGRADE_VERIFYING,
    IOT_UPGRADE_READY,
    IOT_UPGRADE_INSTALLING,
    IOT_UPGRADE_SUCCESS,
    IOT_UPGRADE_FAILED
} iot_upgrade_state_t;

typedef struct {
    uint32_t version;
    uint32_t size;
    uint32_t offset;

    uint8_t sha256[32];

    uint32_t hardware_id;
    uint32_t product_id;
} iot_upgrade_info_t;

typedef struct {
    iot_upgrade_state_t state;

    uint32_t total_size;
    uint32_t received_size;
    uint32_t current_offset;

    int32_t error;
} iot_upgrade_status_t;

typedef void (*iot_upgrade_progress_fn)(const iot_upgrade_status_t *status, void *user);

int iot_upgrade_init(void);

int iot_upgrade_begin(const iot_upgrade_info_t *info);
int iot_upgrade_write(uint32_t offset, const void *data, uint32_t len);
int iot_upgrade_resume(void);
int iot_upgrade_verify(void);
int iot_upgrade_finish(void);
int iot_upgrade_abort(void);

int iot_upgrade_get_status(iot_upgrade_status_t *status);
int iot_upgrade_set_callback(iot_upgrade_progress_fn callback, void *user);

#ifdef __cplusplus
}
#endif

#endif /* IOT_UPGRADE_H */