/*
 * fota_test.c - FOTA 流程测试（对应 fota.c）
 *
 * 覆盖：初始化清空回调、读取源注册、成功升级与钩子调用、
 *       缺少读取源、数据源提前结束、非法参数。
 */

#include "test.h"
#include "fota.h"
#include "upgrade.h"
#include "partition.h"

static const uint8_t s_firmware[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};

static uint32_t s_read_offset;
static int s_apply_calls;
static bool s_short_source;

static int read_source(uint8_t *buf, uint32_t max, void *user)
{
    uint32_t left;

    (void)user;
    if (s_short_source) {
        return 0;
    }
    left = (uint32_t)sizeof(s_firmware) - s_read_offset;
    if (left == 0U) {
        return 0;
    }
    if (left > max) {
        left = max;
    }
    memcpy(buf, &s_firmware[s_read_offset], left);
    s_read_offset += left;
    return (int)left;
}

static void apply_hook(void)
{
    s_apply_calls++;
}

void ut_fota(void)
{
    partition_t ota;
    upgrade_status_t status;

    tf_begin("fota/初始化与回调注册");

    TF_CHECK_INT(fota_init(), 0);
    TF_CHECK_INT(fota_set_read(NULL, NULL), -1);
    TF_CHECK_INT(fota_set_read(read_source, NULL), 0);
    TF_CHECK_INT(fota_set_apply(NULL), 0);

    memset(&ota, 0, sizeof(ota));
    ota.type = PART_OTA;
    ota.address = 0x5000U;
    ota.size = 0x1000U;
    ota.erase_size = 0x400U;
    ota.writable = true;
    TF_CHECK_INT(partition_register(&ota), 0);

    tf_end();

    tf_begin("fota/缺少读取源");

    TF_CHECK_INT(fota_init(), 0);              /* 清空回调 */
    TF_CHECK_INT(fota_run(1U, 32U), -1);

    tf_end();

    tf_begin("fota/成功升级");

    TF_CHECK_INT(fota_init(), 0);
    TF_CHECK_INT(fota_set_read(read_source, NULL), 0);
    TF_CHECK_INT(fota_set_apply(apply_hook), 0);

    s_read_offset = 0;
    s_short_source = false;
    s_apply_calls = 0;

    TF_CHECK_INT(fota_run(0x0100U, (uint32_t)sizeof(s_firmware)), 0);
    TF_CHECK_INT(s_apply_calls, 1);

    TF_CHECK_INT(upgrade_get_status(&status), 0);
    TF_CHECK_INT(status.state, UPGRADE_SUCCESS);
    TF_CHECK_INT(status.received_size, (int)sizeof(s_firmware));

    tf_end();

    tf_begin("fota/参数与数据源异常");

    TF_CHECK_INT(fota_run(1U, 0U), -1);

    /* 数据源提前结束：写入不足，升级流程仍需给出确定结果 */
    TF_CHECK_INT(fota_init(), 0);
    TF_CHECK_INT(fota_set_read(read_source, NULL), 0);
    s_short_source = true;
    s_apply_calls = 0;
    (void)fota_run(1U, 16U);
    TF_CHECK_INT(s_apply_calls, 0);

    tf_end();
}
