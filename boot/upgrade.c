#include "upgrade.h"
#include "partition.h"
#include <string.h>

static upgrade_state_t s_state = UPGRADE_IDLE;
static uint32_t s_total_size = 0;
static uint32_t s_received_size = 0;
static upgrade_progress_fn s_callback = NULL;
static void *s_callback_user = NULL;

static void upgrade_progress(void)
{
    upgrade_status_t st;

    if (s_callback == NULL) {
        return;
    }
    st.state = s_state;
    st.total_size = s_total_size;
    st.received_size = s_received_size;
    st.current_offset = s_received_size;
    st.error = 0;
    s_callback(&st, s_callback_user);
}

int upgrade_init(void)
{
    s_state = UPGRADE_IDLE;
    s_total_size = 0;
    s_received_size = 0;
    s_callback = NULL;
    s_callback_user = NULL;
    return IOT_OK;
}

int upgrade_begin(const upgrade_info_t *info)
{
    const partition_t *p;

    if (info == NULL || info->size == 0) {
        return -1;
    }
    p = partition_get(PART_OTA);
    if (p == NULL || info->size > p->size) {
        return -1;
    }

    s_state = UPGRADE_PREPARE;
    (void)partition_erase(PART_OTA, 0, p->size);

    s_total_size = info->size;
    s_received_size = 0;
    s_state = UPGRADE_RECEIVING;
    upgrade_progress();
    return IOT_OK;
}

int upgrade_write(uint32_t offset, const void *data, uint32_t len)
{
    int r;

    if (data == NULL || len == 0) {
        return -1;
    }
    if (s_state != UPGRADE_RECEIVING) {
        return -1;
    }
    if (offset + len > s_total_size) {
        return -1;
    }
    r = partition_write(PART_OTA, offset, data, len);
    if (r != IOT_OK) {
        s_state = UPGRADE_FAILED;
        return r;
    }
    if (offset + len > s_received_size) {
        s_received_size = offset + len;
    }
    upgrade_progress();
    return IOT_OK;
}

int upgrade_resume(void)
{
    if (s_state == UPGRADE_RECEIVING) {
        return IOT_OK;
    }
    s_state = UPGRADE_RECEIVING;
    return IOT_OK;
}

int upgrade_verify(void)
{
    if (s_state != UPGRADE_RECEIVING && s_state != UPGRADE_READY) {
        return -1;
    }
    s_state = UPGRADE_VERIFYING;
    if (s_received_size < s_total_size) {
        s_state = UPGRADE_FAILED;
        return -1;
    }
    s_state = UPGRADE_READY;
    return IOT_OK;
}

int upgrade_finish(void)
{
    if (s_state != UPGRADE_READY) {
        return -1;
    }
    s_state = UPGRADE_SUCCESS;
    upgrade_progress();
    return IOT_OK;
}

int upgrade_abort(void)
{
    s_state = UPGRADE_IDLE;
    return IOT_OK;
}

int upgrade_get_status(upgrade_status_t *status)
{
    if (status == NULL) {
        return -1;
    }
    status->state = s_state;
    status->total_size = s_total_size;
    status->received_size = s_received_size;
    status->current_offset = s_received_size;
    status->error = 0;
    return IOT_OK;
}

int upgrade_set_callback(upgrade_progress_fn callback, void *user)
{
    s_callback = callback;
    s_callback_user = user;
    return IOT_OK;
}