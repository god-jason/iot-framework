#ifndef IOT_FOTA_H
#define IOT_FOTA_H

/*
 * FOTA 远程固件升级：下载固件 -> 写入升级分区 -> 校验 -> 请求重启升级。
 * 下载源通过数据源回调解耦（可接 HTTP / UART / 串口升级等）。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 读取一块固件数据：返回该块字节数，0 表示结束，<0 表示错误 */
typedef int (*iot_fota_read_fn)(uint8_t *buf, uint32_t max, void *user);

/* 应用升级钩子：请求重启进入 Bootloader */
typedef void (*iot_fota_apply_fn)(void);

int iot_fota_init(void);

int iot_fota_set_read(iot_fota_read_fn fn, void *user);
int iot_fota_set_apply(iot_fota_apply_fn fn);

/* 执行一次升级：下载 + 写入 + 校验 + 请求重启 */
int iot_fota_run(uint32_t version, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* IOT_FOTA_H */