/*
 * fota_test.c - FOTA 流程测试（对应 fota.c）
 *
 * 覆盖：初始化清空回调、读取源注册、成功升级与钩子调用、
 *       缺少读取源、数据源提前结束、非法参数。
 */

#include "test.h"
#include "fota.h"
#include "bootloader.h"

#define TEST_PAYLOAD 16U

static uint8_t  s_firmware[sizeof(image_header_t) + TEST_PAYLOAD];
static uint32_t s_firmware_size;
static uint32_t s_read_offset;
static int      s_apply_calls;
static bool     s_short_source;

/* 构造一个带合法镜像头与 CRC 的固件，供升级流程校验 */
static void build_firmware(uint32_t version)
{
    image_header_t header;
    uint32_t i;

    for (i = 0U; i < TEST_PAYLOAD; i++) {
        s_firmware[sizeof(header) + i] = (uint8_t)(0xA0U + i);
    }
    memset(&header, 0, sizeof(header));
    header.magic = IMAGE_MAGIC;
    header.header_size = (uint16_t)sizeof(header);
    header.header_version = 1U;
    header.version = version;
    header.image_size = TEST_PAYLOAD;
    header.crc32 = image_crc32(&s_firmware[sizeof(header)], TEST_PAYLOAD);
    memcpy(s_firmware, &header, sizeof(header));

    s_firmware_size = (uint32_t)sizeof(header) + TEST_PAYLOAD;
}

static int read_source(uint8_t *buf, uint32_t max, void *user)
{
    uint32_t left;

    (void)user;
    if (s_short_source) {
        return 0;
    }
    left = s_firmware_size - s_read_offset;
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

    /* 与默认布局一致地重新注册 OTA，验证注册路径且不影响其它分区 */
    memset(&ota, 0, sizeof(ota));
    ota.type = PART_OTA;
    ota.address = 0xB000U;
    ota.size = 0x4000U;
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

    build_firmware(0x0100U);
    TF_CHECK_INT(fota_run(0x0100U, s_firmware_size), 0);
    TF_CHECK_INT(s_apply_calls, 1);

    TF_CHECK_INT(upgrade_get_status(&status), 0);
    TF_CHECK_INT(status.state, UPGRADE_SUCCESS);
    TF_CHECK_INT(status.received_size, (int)s_firmware_size);

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
