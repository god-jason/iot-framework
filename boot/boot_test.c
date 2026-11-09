/*
 * boot_test.c - Bootloader 逻辑测试（对应 boot.c）
 *
 * 覆盖：初始化模式、升级/回滚请求对模式的影响、镜像选择、
 *       跳转钩子注册与应用启动、未注册分区时的失败。
 */

#include "test.h"
#include "boot.h"
#include "partition.h"
#include "image.h"

static int s_jump_calls;

static void jump_hook(void)
{
    s_jump_calls++;
}

void ut_boot(void)
{
    partition_t ota;

    tf_begin("boot/初始化与模式");

    TF_CHECK_INT(boot_init(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_NORMAL);

    TF_CHECK_INT(boot_select_image(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_NORMAL);

    TF_CHECK_INT(boot_request_upgrade(), 0);
    TF_CHECK_INT(boot_select_image(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_UPGRADE);

    tf_end();

    tf_begin("boot/回滚请求");

    TF_CHECK_INT(boot_request_rollback(), 0);
    TF_CHECK_INT(boot_select_image(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_ROLLBACK);

    tf_end();

    tf_begin("boot/跳转钩子");

    TF_CHECK_INT(boot_init(), 0);
    s_jump_calls = 0;
    TF_CHECK_INT(boot_set_jump(jump_hook), 0);
    TF_CHECK_INT(boot_start_application(), 0);
    TF_CHECK_INT(s_jump_calls, 1);

    /* 未注册钩子：启动失败 */
    TF_CHECK_INT(boot_set_jump(NULL), 0);
    TF_CHECK_INT(boot_start_application(), -1);

    tf_end();

    tf_begin("boot/镜像校验");

    /* 未注册 OTA 分区时读取失败 */
    TF_CHECK_INT(boot_verify_image(), -1);

    memset(&ota, 0, sizeof(ota));
    ota.type = PART_OTA;
    ota.address = 0x5000U;
    ota.size = 0x2000U;
    ota.erase_size = 0x400U;
    ota.writable = true;
    TF_CHECK_INT(partition_register(&ota), 0);

    /* 分区内容未初始化（全零）：magic 不匹配 */
    TF_CHECK_INT(boot_verify_image(), -1);

    {
        image_header_t header;
        uint8_t data[sizeof(image_header_t)];

        memset(&header, 0, sizeof(header));
        header.magic = IMAGE_MAGIC;
        header.header_size = (uint32_t)sizeof(image_header_t);
        header.image_size = 0x1000U;
        memcpy(data, &header, sizeof(header));

        TF_CHECK_INT(partition_write(PART_OTA, 0U, data, sizeof(data)), 0);
        TF_CHECK_INT(boot_verify_image(), 0);
    }

    tf_end();
}
