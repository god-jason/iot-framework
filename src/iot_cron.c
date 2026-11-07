#include "iot_cron.h"
#include <string.h>
#include <stdlib.h>
#include "iot_datetime.h"

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
    char *end = NULL;
    unsigned long v;

    if (s == NULL || *s == '\0') {
        return -1;
    }
    if (*s == '*') {
        *any = true;
        return IOT_OK;
    }
    v = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || v > max) {
        return -1;
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
        return -1;
    }
    strncpy(buf, expression, sizeof(buf) - 1u);
    buf[sizeof(buf) - 1u] = '\0';
    {
        char *p = buf;
        char *c1 = strchr(p, ':');
        char *c2;

        if (c1 == NULL) {
            return -1;
        }
        *c1 = '\0';
        c2 = strchr(c1 + 1, ':');
        if (c2 == NULL || strchr(c2 + 1, ':') != NULL) {
            return -1;
        }
        *c2 = '\0';
        fields[0] = p;
        fields[1] = c1 + 1;
        fields[2] = c2 + 1;
        count = 3;
    }
    if (count != 3) {
        return -1;
    }
    for (i = 0; i < IOT_CRON_MAX; i++) {
        if (!s_entries[i].used) {
            break;
        }
    }
    if (i == IOT_CRON_MAX) {
        return -1;
    }

    e = &s_entries[i];
    if (cron_parse_field(fields[0], &e->hour, &e->hour_any, 23) != IOT_OK ||
        cron_parse_field(fields[1], &e->minute, &e->minute_any, 59) != IOT_OK ||
        cron_parse_field(fields[2], &e->second, &e->second_any, 59) != IOT_OK) {
        return -1;
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
        return -1;
    }
    s_entries[id].used = false;
    return IOT_OK;
}

int iot_cron_process(const iot_datetime_t *now)
{
    int64_t ts;
    int i;

    if (now == NULL) {
        return -1;
    }
    if (iot_datetime_to_timestamp(now, &ts) != IOT_OK) {
        return -1;
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