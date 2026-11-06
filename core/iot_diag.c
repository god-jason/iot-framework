#include "iot_diag.h"

#define IOT_DIAG_MAX 16

static iot_diag_record_t s_records[IOT_DIAG_MAX];
static uint32_t s_head = 0;
static uint32_t s_count = 0;
static iot_tick_t s_tick = 0;

int iot_diag_init(void)
{
    s_head = 0;
    s_count = 0;
    s_tick = 0;
    return IOT_OK;
}

int iot_diag_report(uint32_t code, iot_diag_level_t level,
                    const char *module, const char *message)
{
    iot_diag_record_t *r;

    if (message == NULL) {
        return IOT_ERR_PARAM;
    }
    r = &s_records[s_head];
    r->code = code;
    r->level = level;
    r->module = module;
    r->message = message;
    r->timestamp = s_tick++;

    s_head = (s_head + 1) % IOT_DIAG_MAX;
    if (s_count < IOT_DIAG_MAX) {
        s_count++;
    }
    return IOT_OK;
}

int iot_diag_get(uint32_t index, iot_diag_record_t *record)
{
    uint32_t oldest, idx;

    if (record == NULL || index >= s_count) {
        return IOT_ERR_PARAM;
    }
    oldest = (s_head + IOT_DIAG_MAX - s_count) % IOT_DIAG_MAX;
    idx = (oldest + index) % IOT_DIAG_MAX;
    *record = s_records[idx];
    return IOT_OK;
}

int iot_diag_clear(void)
{
    s_head = 0;
    s_count = 0;
    return IOT_OK;
}

uint32_t iot_diag_count(void)
{
    return s_count;
}