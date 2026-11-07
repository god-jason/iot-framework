#ifndef UPGRADE_H
#define UPGRADE_H

/*
 * 升级管理器：接收固件 → 写入 OTA 分区 → 校验 → 完成/中止。
 * 与 Bootloader（boot）分离：本模块负责下载/暂存/校验。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UPGRADE_IDLE,
    UPGRADE_PREPARE,
    UPGRADE_RECEIVING,
    UPGRADE_VERIFYING,
    UPGRADE_READY,
    UPGRADE_INSTALLING,
    UPGRADE_SUCCESS,
    UPGRADE_FAILED
} upgrade_state_t;

typedef struct {
    uint32_t version;
    uint32_t size;
    uint32_t offset;

    uint8_t sha256[32];

    uint32_t hardware_id;
    uint32_t product_id;
} upgrade_info_t;

typedef struct {
    upgrade_state_t state;

    uint32_t total_size;
    uint32_t received_size;
    uint32_t current_offset;

    int32_t error;
} upgrade_status_t;

typedef void (*upgrade_progress_fn)(const upgrade_status_t *status, void *user);

int upgrade_init(void);

int upgrade_begin(const upgrade_info_t *info);
int upgrade_write(uint32_t offset, const void *data, uint32_t len);
int upgrade_resume(void);
int upgrade_verify(void);
int upgrade_finish(void);
int upgrade_abort(void);

int upgrade_get_status(upgrade_status_t *status);
int upgrade_set_callback(upgrade_progress_fn callback, void *user);

#ifdef __cplusplus
}
#endif

#endif /* UPGRADE_H */