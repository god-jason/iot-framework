#include "iot_cron.h"
#include "iot_string.h"
#include "iot_datetime.h"
#include "iot_memory.h"

#define IOT_CRON_MAX 8

typedef struct {
    uint8_t hour, minute, second;
    bool hour_any, minute_any, second_any;
    iot_cron_fn callback;
    void *user;
    bool used;
    int64_t last_fire;
} cron_entry_t;

static cron_entry_t s_entries[IOT_CRON_MAX];

static int cron_parse_field(const char *s, uint8_t *val, bool *any, uint32_t max)
{
    uint32_t v;

    if (s == NULL || *s == '\0') {
        return IOT_ERR_INVALID;
    }
    if (*s == '*') {
        *any = true;
        return IOT_OK;
    }
    if (iot_string_to_uint(s, &v) != IOT_OK || v > max) {
        return IOT_ERR_INVALID;
    }
    *val = (uint8_t)v;
    *any = false;
    return IOT_OK;
}

int iot_cron_init(void)
{
    int i;
    for (i = 0; i < IOT_CRON_MAX; i++) {
        s_entries[i].used = false;
    }
    return IOT_OK;
}

int iot_cron_add(const char *expression, iot_cron_fn callback,
                 void *user, iot_id_t *id)
{
    char buf[16];
    char *fields[3];
    uint32_t count = 0;
    cron_entry_t *e;
    int i;

    if (expression == NULL || callback == NULL) {
        return IOT_ERR_PARAM;
    }
    iot_string_ncopy(buf, expression, sizeof(buf));
    if (iot_string_split(buf, ':', fields, 3, &count) != IOT_OK || count != 3) {
        return IOT_ERR_INVALID;
    }
    for (i = 0; i < IOT_CRON_MAX; i++) {
        if (!s_entries[i].used) {
            break;
        }
    }
    if (i == IOT_CRON_MAX) {
        return IOT_ERR_OVERFLOW;
    }

    e = &s_entries[i];
    if (cron_parse_field(fields[0], &e->hour, &e->hour_any, 23) != IOT_OK ||
        cron_parse_field(fields[1], &e->minute, &e->minute_any, 59) != IOT_OK ||
        cron_parse_field(fields[2], &e->second, &e->second_any, 59) != IOT_OK) {
        return IOT_ERR_INVALID;
    }
    e->callback = callback;
    e->user = user;
    e->used = true;
    e->last_fire = 0;

    if (id != NULL) {
        *id = (iot_id_t)i;
    }
    return IOT_OK;
}

int iot_cron_remove(iot_id_t id)
{
    if (id >= IOT_CRON_MAX || !s_entries[id].used) {
        return IOT_ERR_NOT_FOUND;
    }
    s_entries[id].used = false;
    return IOT_OK;
}

int iot_cron_process(const iot_datetime_t *now)
{
    int64_t ts;
    int i;

    if (now == NULL) {
        return IOT_ERR_PARAM;
    }
    if (iot_datetime_to_timestamp(now, &ts) != IOT_OK) {
        return IOT_ERR_INVALID;
    }

    for (i = 0; i < IOT_CRON_MAX; i++) {
        cron_entry_t *e = &s_entries[i];
        bool match;

        if (!e->used) {
            continue;
        }
        match = (e->hour_any || now->hour == e->hour) &&
                (e->minute_any || now->minute == e->minute) &&
                (e->second_any || now->second == e->second);

        if (match && ts != e->last_fire) {
            e->last_fire = ts;
            if (e->callback != NULL) {
                e->callback(e->user);
            }
        }
    }
    return IOT_OK;
}