#include "iot_fota.h"
#include "iot_upgrade.h"
#include "iot_boot.h"
#include "iot_logger.h"
#include <string.h>

static iot_fota_read_fn s_read = NULL;
static void *s_read_user = NULL;
static iot_fota_apply_fn s_apply = NULL;

int iot_fota_init(void)
{
    s_read = NULL;
    s_read_user = NULL;
    s_apply = NULL;
    return iot_upgrade_init();
}

int iot_fota_set_read(iot_fota_read_fn fn, void *user)
{
    if (fn == NULL) {
        return IOT_ERR_PARAM;
    }
    s_read = fn;
    s_read_user = user;
    return IOT_OK;
}

int iot_fota_set_apply(iot_fota_apply_fn fn)
{
    s_apply = fn;
    return IOT_OK;
}

int iot_fota_run(uint32_t version, uint32_t size)
{
    iot_upgrade_info_t info;
    uint8_t buf[256];
    uint32_t offset = 0;
    int n;

    if (s_read == NULL || size == 0) {
        LOG_E("fota", "invalid read source");   /* fail: no read source */
        return IOT_ERR_PARAM;
    }

    memset(&info, 0, sizeof(info));
    info.version = version;
    info.size = size;

    if (iot_upgrade_begin(&info) != IOT_OK) {
        LOG_E("fota", "upgrade begin failed");   /* fail: begin */
        return IOT_ERR_STATE;
    }
    LOG_I("fota", "downloading %u bytes", (unsigned)size);   /* downloading */

    while (offset < size) {
        uint32_t max = sizeof(buf);
        if (size - offset < max) {
            max = size - offset;
        }
        n = s_read(buf, max, s_read_user);
        if (n < 0) {
            LOG_E("fota", "read error at %u", (unsigned)offset);   /* fail: read */
            iot_upgrade_abort();
            return IOT_ERR_IO;
        }
        if (n == 0) {
            break;
        }
        if (iot_upgrade_write(offset, buf, (uint32_t)n) != IOT_OK) {
            LOG_E("fota", "write error at %u", (unsigned)offset);   /* fail: write */
            iot_upgrade_abort();
            return IOT_ERR_IO;
        }
        offset += (uint32_t)n;
    }

    if (iot_upgrade_verify() != IOT_OK) {
        LOG_E("fota", "verify failed");   /* fail: verify */
        return IOT_ERR_CRC;
    }
    if (iot_upgrade_finish() != IOT_OK) {
        LOG_E("fota", "finish failed");   /* fail: finish */
        return IOT_ERR_STATE;
    }

    LOG_I("fota", "upgrade ready, apply");   /* ready to apply */
    if (s_apply != NULL) {
        s_apply();
    } else {
        iot_boot_request_upgrade();
    }
    return IOT_OK;
}