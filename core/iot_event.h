#ifndef IOT_EVENT_H
#define IOT_EVENT_H

/*
 * 事件系统（发布 / 订阅 / 分发）。
 * publish() 为同步立即分发；publish_isr() 仅入队，由主循环 process() 分发。
 * 注意：异步事件的数据指针在 process() 执行前必须保持有效。
 */

#include "iot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t iot_event_id_t;

typedef struct {
    iot_event_id_t id;
    void *data;
    uint32_t size;
    void *sender;
} iot_event_t;

typedef void (*iot_event_fn)(const iot_event_t *event, void *user);

int iot_event_init(void);

int iot_event_subscribe(iot_event_id_t id, iot_event_fn callback, void *user);
int iot_event_unsubscribe(iot_event_id_t id, iot_event_fn callback);

int iot_event_publish(iot_event_id_t id, void *data, uint32_t size);
int iot_event_publish_isr(iot_event_id_t id, void *data, uint32_t size);

int iot_event_process(void);

uint32_t iot_event_listener_count(void);

#ifdef __cplusplus
}
#endif

#endif /* IOT_EVENT_H */