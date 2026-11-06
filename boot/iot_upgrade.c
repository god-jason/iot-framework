#include "iot_upgrade.h"
#include "iot_partition.h"
#include "iot_memory.h"

static iot_upgrade_state_t s_state = IOT_UPGRADE_IDLE;
static uint32_t s_total_size = 0;
static uint32_t s_received_size = 0;
static iot_upgrade_progress_fn s_callback = NULL;
static void *s_callback_user = NULL;

static void upgrade_progress(void)
{
    iot_upgrade_status_t st;

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

int iot_upgrade_init(void)
{
    s_state = IOT_UPGRADE_IDLE;
    s_total_size = 0;
    s_received_size = 0;
    s_callback = NULL;
    s_callback_user = NULL;
    return IOT_OK;
}

int iot_upgrade_begin(const iot_upgrade_info_t *info)
{
    const iot_partition_t *p;

    if (info == NULL || info->size == 0) {
        return IOT_ERR_PARAM;
    }
    p = iot_partition_get(IOT_PART_OTA);
    if (p == NULL || info->size > p->size) {
        return IOT_ERR_OVERFLOW;
    }

    s_state = IOT_UPGRADE_PREPARE;
    (void)iot_partition_erase(IOT_PART_OTA, 0, p->size);

    s_total_size = info->size;
    s_received_size = 0;
    s_state = IOT_UPGRADE_RECEIVING;
    upgrade_progress();
    return IOT_OK;
}

int iot_upgrade_write(uint32_t offset, const void *data, uint32_t len)
{
    int r;

    if (data == NULL || len == 0) {
        return IOT_ERR_PARAM;
    }
    if (s_state != IOT_UPGRADE_RECEIVING) {
        return IOT_ERR_STATE;
    }
    if (offset + len > s_total_size) {
        return IOT_ERR_OVERFLOW;
    }
    r = iot_partition_write(IOT_PART_OTA, offset, data, len);
    if (r != IOT_OK) {
        s_state = IOT_UPGRADE_FAILED;
        return r;
    }
    if (offset + len > s_received_size) {
        s_received_size = offset + len;
    }
    upgrade_progress();
    return IOT_OK;
}

int iot_upgrade_resume(void)
{
    if (s_state == IOT_UPGRADE_RECEIVING) {
        return IOT_OK;
    }
    s_state = IOT_UPGRADE_RECEIVING;
    return IOT_OK;
}

int iot_upgrade_verify(void)
{
    if (s_state != IOT_UPGRADE_RECEIVING && s_state != IOT_UPGRADE_READY) {
        return IOT_ERR_STATE;
    }
    s_state = IOT_UPGRADE_VERIFYING;
    if (s_received_size < s_total_size) {
        s_state = IOT_UPGRADE_FAILED;
        return IOT_ERR_IO;
    }
    s_state = IOT_UPGRADE_READY;
    return IOT_OK;
}

int iot_upgrade_finish(void)
{
    if (s_state != IOT_UPGRADE_READY) {
        return IOT_ERR_STATE;
    }
    s_state = IOT_UPGRADE_SUCCESS;
    upgrade_progress();
    return IOT_OK;
}

int iot_upgrade_abort(void)
{
    s_state = IOT_UPGRADE_IDLE;
    return IOT_OK;
}

int iot_upgrade_get_status(iot_upgrade_status_t *status)
{
    if (status == NULL) {
        return IOT_ERR_PARAM;
    }
    status->state = s_state;
    status->total_size = s_total_size;
    status->received_size = s_received_size;
    status->current_offset = s_received_size;
    status->error = 0;
    return IOT_OK;
}

int iot_upgrade_set_callback(iot_upgrade_progress_fn callback, void *user)
{
    s_callback = callback;
    s_callback_user = user;
    return IOT_OK;
}