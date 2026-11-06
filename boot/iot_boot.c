/* Bootloader 通用逻辑：模式管理、镜像选择/安装、升级/回滚请求。
 * 真正的 Flash 写入与跳转属平台相关，通过 jump 钩子或分区表接入。 */
#include "iot_boot.h"
#include "iot_image.h"
#include "iot_partition.h"
#include "iot_memory.h"

static iot_boot_mode_t s_mode = IOT_BOOT_NORMAL;
static bool s_upgrade_requested = false;
static bool s_rollback_requested = false;
static bool s_app_confirmed = false;
static iot_boot_jump_fn s_jump = NULL;

int iot_boot_set_jump(iot_boot_jump_fn jump)
{
    s_jump = jump;
    return IOT_OK;
}

int iot_boot_init(void)
{
    s_mode = IOT_BOOT_NORMAL;
    s_upgrade_requested = false;
    s_rollback_requested = false;
    s_app_confirmed = true;
    return IOT_OK;
}

iot_boot_mode_t iot_boot_get_mode(void)
{
    return s_mode;
}

int iot_boot_select_image(void)
{
    if (s_rollback_requested) {
        s_mode = IOT_BOOT_ROLLBACK;
    } else if (s_upgrade_requested) {
        s_mode = IOT_BOOT_UPGRADE;
    } else {
        s_mode = IOT_BOOT_NORMAL;
    }
    return IOT_OK;   /* log: boot image selected */
}

int iot_boot_verify_image(void)
{
    iot_image_header_t header;
    uint8_t buf[sizeof(iot_image_header_t)];

    /* 读取 OTA 分区头部并校验 */
    if (iot_partition_read(IOT_PART_OTA, 0, buf, sizeof(buf)) != IOT_OK) {
        return IOT_ERR_IO;
    }
    if (iot_image_parse(buf, sizeof(buf), &header) != IOT_OK) {
        return IOT_ERR_INVALID;
    }
    return iot_image_verify(&header);   /* log: boot image verified */
}

int iot_boot_install_image(void)
{
    const iot_partition_t *app = iot_partition_get(IOT_PART_APP);
    const iot_partition_t *ota = iot_partition_get(IOT_PART_OTA);
    uint8_t block[64];
    uint32_t off;

    if (app == NULL || ota == NULL) {
        return IOT_ERR_NOT_FOUND;
    }
    if (!app->writable) {
        return IOT_ERR_STATE;
    }
    /* 分块拷贝 OTA -> APP（真实平台为 Flash 擦写） */
    for (off = 0; off < ota->size; off += sizeof(block)) {
        uint32_t n = (ota->size - off > sizeof(block)) ? sizeof(block) : (ota->size - off);
        if (iot_partition_read(IOT_PART_OTA, off, block, n) != IOT_OK ||
            iot_partition_write(IOT_PART_APP, off, block, n) != IOT_OK) {
            return IOT_ERR_IO;
        }
    }
    return IOT_OK;   /* log: boot image installed */
}

int iot_boot_start_application(void)
{
    if (s_jump == NULL) {
        return IOT_ERR_NOT_SUPPORTED;   /* no platform jump hook */
    }
    s_jump();   /* log: boot jumping to app */
    return IOT_OK;
}

int iot_boot_request_upgrade(void)
{
    s_upgrade_requested = true;
    return IOT_OK;
}

int iot_boot_request_rollback(void)
{
    s_rollback_requested = true;
    return IOT_OK;
}

int iot_boot_confirm_application(void)
{
    s_app_confirmed = true;
    s_upgrade_requested = false;
    s_rollback_requested = false;
    return IOT_OK;
}