#include "iot_event.h"

#define IOT_EVENT_MAX_LISTENERS 16
#define IOT_EVENT_MAX_PENDING   16

typedef struct {
    iot_event_id_t event_id;
    iot_event_fn callback;
    void *user;
} listener_t;

static listener_t s_listeners[IOT_EVENT_MAX_LISTENERS];

static iot_event_t s_pending[IOT_EVENT_MAX_PENDING];
static uint32_t s_pending_head = 0;
static uint32_t s_pending_count = 0;

int iot_event_init(void)
{
    int i;
    for (i = 0; i < IOT_EVENT_MAX_LISTENERS; i++) {
        s_listeners[i].event_id = 0;
        s_listeners[i].callback = NULL;
        s_listeners[i].user = NULL;
    }
    s_pending_head = 0;
    s_pending_count = 0;
    return IOT_OK;
}

static void dispatch(const iot_event_t *event)
{
    int i;
    for (i = 0; i < IOT_EVENT_MAX_LISTENERS; i++) {
        if (s_listeners[i].callback != NULL &&
            s_listeners[i].event_id == event->id) {
            s_listeners[i].callback(event, s_listeners[i].user);
        }
    }
}

int iot_event_subscribe(iot_event_id_t id, iot_event_fn callback, void *user)
{
    int i;
    if (callback == NULL) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < IOT_EVENT_MAX_LISTENERS; i++) {
        if (s_listeners[i].callback == NULL) {
            s_listeners[i].event_id = id;
            s_listeners[i].callback = callback;
            s_listeners[i].user = user;
            return IOT_OK;
        }
    }
    return IOT_ERR_OVERFLOW;
}

int iot_event_unsubscribe(iot_event_id_t id, iot_event_fn callback)
{
    int i;
    if (callback == NULL) {
        return IOT_ERR_PARAM;
    }
    for (i = 0; i < IOT_EVENT_MAX_LISTENERS; i++) {
        if (s_listeners[i].callback == callback &&
            s_listeners[i].event_id == id) {
            s_listeners[i].event_id = 0;
            s_listeners[i].callback = NULL;
            s_listeners[i].user = NULL;
            return IOT_OK;
        }
    }
    return IOT_ERR_NOT_FOUND;
}

int iot_event_publish(iot_event_id_t id, void *data, uint32_t size)
{
    iot_event_t event;
    event.id = id;
    event.data = data;
    event.size = size;
    event.sender = NULL;
    dispatch(&event);
    return IOT_OK;
}

int iot_event_publish_isr(iot_event_id_t id, void *data, uint32_t size)
{
    uint32_t idx;
    if (s_pending_count >= IOT_EVENT_MAX_PENDING) {
        return IOT_ERR_OVERFLOW;
    }
    idx = (s_pending_head + s_pending_count) % IOT_EVENT_MAX_PENDING;
    s_pending[idx].id = id;
    s_pending[idx].data = data;
    s_pending[idx].size = size;
    s_pending[idx].sender = NULL;
    s_pending_count++;
    return IOT_OK;
}

int iot_event_process(void)
{
    while (s_pending_count > 0) {
        iot_event_t ev = s_pending[s_pending_head];
        s_pending_head = (s_pending_head + 1) % IOT_EVENT_MAX_PENDING;
        s_pending_count--;
        dispatch(&ev);
    }
    return IOT_OK;
}

uint32_t iot_event_listener_count(void)
{
    uint32_t n = 0;
    int i;
    for (i = 0; i < IOT_EVENT_MAX_LISTENERS; i++) {
        if (s_listeners[i].callback != NULL) {
            n++;
        }
    }
    return n;
}