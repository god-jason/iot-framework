/* Bootloader 通用逻辑：模式管理、镜像选择/安装、升级/回滚请求。
 * 真正的 Flash 写入与跳转属平台相关，通过 jump 钩子或分区表接入。 */
#include "boot.h"
#include "image.h"
#include "partition.h"
#include <string.h>

static boot_mode_t s_mode = BOOT_NORMAL;
static bool s_upgrade_requested = false;
static bool s_rollback_requested = false;
static bool s_app_confirmed = false;
static boot_jump_fn s_jump = NULL;

int boot_set_jump(boot_jump_fn jump)
{
    s_jump = jump;
    return IOT_OK;
}

int boot_init(void)
{
    s_mode = BOOT_NORMAL;
    s_upgrade_requested = false;
    s_rollback_requested = false;
    s_app_confirmed = true;
    return IOT_OK;
}

boot_mode_t boot_get_mode(void)
{
    return s_mode;
}

int boot_select_image(void)
{
    if (s_rollback_requested) {
        s_mode = BOOT_ROLLBACK;
    } else if (s_upgrade_requested) {
        s_mode = BOOT_UPGRADE;
    } else {
        s_mode = BOOT_NORMAL;
    }
    return IOT_OK;   /* log: boot image selected */
}

int boot_verify_image(void)
{
    image_header_t header;
    uint8_t buf[sizeof(image_header_t)];

    /* 读取 OTA 分区头部并校验 */
    if (partition_read(PART_OTA, 0, buf, sizeof(buf)) != IOT_OK) {
        return -1;
    }
    if (image_parse(buf, sizeof(buf), &header) != IOT_OK) {
        return -1;
    }
    return image_verify(&header);   /* log: boot image verified */
}

int boot_install_image(void)
{
    const partition_t *app = partition_get(PART_APP);
    const partition_t *ota = partition_get(PART_OTA);
    uint8_t block[64];
    uint32_t off;

    if (app == NULL || ota == NULL) {
        return -1;
    }
    if (!app->writable) {
        return -1;
    }
    /* 分块拷贝 OTA -> APP（真实平台为 Flash 擦写） */
    for (off = 0; off < ota->size; off += sizeof(block)) {
        uint32_t n = (ota->size - off > sizeof(block)) ? sizeof(block) : (ota->size - off);
        if (partition_read(PART_OTA, off, block, n) != IOT_OK ||
            partition_write(PART_APP, off, block, n) != IOT_OK) {
            return -1;
        }
    }
    return IOT_OK;   /* log: boot image installed */
}

int boot_start_application(void)
{
    if (s_jump == NULL) {
        return -1;   /* no platform jump hook */
    }
    s_jump();   /* log: boot jumping to app */
    return IOT_OK;
}

int boot_request_upgrade(void)
{
    s_upgrade_requested = true;
    return IOT_OK;
}

int boot_request_rollback(void)
{
    s_rollback_requested = true;
    return IOT_OK;
}

int boot_confirm_application(void)
{
    s_app_confirmed = true;
    s_upgrade_requested = false;
    s_rollback_requested = false;
    return IOT_OK;
}