#include "fota.h"
#include "upgrade.h"
#include "boot.h"
#include "iot_logger.h"
#include <string.h>

static fota_read_fn s_read = NULL;
static void *s_read_user = NULL;
static fota_apply_fn s_apply = NULL;

int fota_init(void)
{
    s_read = NULL;
    s_read_user = NULL;
    s_apply = NULL;
    return upgrade_init();
}

int fota_set_read(fota_read_fn fn, void *user)
{
    if (fn == NULL) {
        return -1;
    }
    s_read = fn;
    s_read_user = user;
    return IOT_OK;
}

int fota_set_apply(fota_apply_fn fn)
{
    s_apply = fn;
    return IOT_OK;
}

int fota_run(uint32_t version, uint32_t size)
{
    upgrade_info_t info;
    uint8_t buf[256];
    uint32_t offset = 0;
    int n;

    if (s_read == NULL || size == 0) {
        LOG_E("fota", "invalid read source");   /* fail: no read source */
        return -1;
    }

    memset(&info, 0, sizeof(info));
    info.version = version;
    info.size = size;

    if (upgrade_begin(&info) != IOT_OK) {
        LOG_E("fota", "upgrade begin failed");   /* fail: begin */
        return -1;
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
            upgrade_abort();
            return -1;
        }
        if (n == 0) {
            break;
        }
        if (upgrade_write(offset, buf, (uint32_t)n) != IOT_OK) {
            LOG_E("fota", "write error at %u", (unsigned)offset);   /* fail: write */
            upgrade_abort();
            return -1;
        }
        offset += (uint32_t)n;
    }

    if (upgrade_verify() != IOT_OK) {
        LOG_E("fota", "verify failed");   /* fail: verify */
        return -1;
    }
    if (upgrade_finish() != IOT_OK) {
        LOG_E("fota", "finish failed");   /* fail: finish */
        return -1;
    }

    LOG_I("fota", "upgrade ready, apply");   /* ready to apply */
    if (s_apply != NULL) {
        s_apply();
    } else {
        boot_request_upgrade();
    }
    return IOT_OK;
}