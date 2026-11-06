#ifndef IOT_CRON_H
#define IOT_CRON_H

/*
 * 定时任务（crontab 简化版）。
 * 表达式格式 "HH:MM:SS"（每天定时），字段可用 '*' 表示通配。
 * 例："08:00:00" 每天 8 点触发；"*:*:00" 每分钟第 0 秒触发。
 */

#include "iot_types.h"
#include "iot_datetime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*iot_cron_fn)(void *user);

int iot_cron_init(void);

int iot_cron_add(const char *expression, iot_cron_fn callback,
                 void *user, iot_id_t *id);

int iot_cron_remove(iot_id_t id);

/* 由主循环传入当前时间，检查并触发到期任务 */
int iot_cron_process(const iot_datetime_t *now);

#ifdef __cplusplus
}
#endif

#endif /* IOT_CRON_H */