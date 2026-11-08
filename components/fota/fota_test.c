/*
 * FOTA 组件测试。
 *
 * 说明：fota.c 依赖 boot 模块的 OTA 分区（partition_register/partition_get）。
 * 本测试通过公开接口注册一块 16KB 的可写 PART_OTA 分区来模拟升级区，
 * 数据源用回调模拟固件分片。实现中并没有 CRC/SHA 校验，upgrade_verify()
 * 只检查「已接收长度是否达到声明长度」，因此这里按真实行为断言。
 * 依赖真实 MCU Flash 驱动与复位跳转的部分（真实擦写、boot 跳转）不在主机测试范围内。
 */
#include "test.h"
#include "fota.h"
#include "upgrade.h"
#include "boot.h"
#include "partition.h"

#include <string.h>

#define FW_SIZE      600u
#define OTA_ADDR     0x4000u
#define OTA_SIZE     0x4000u

static uint8_t s_fw[FW_SIZE];
static int s_apply_calls;

typedef struct {
    const uint8_t *data;
    uint32_t total;
    uint32_t pos;
    int calls;
    int mode;          /* 0=正常 1=立即读错误 2=立即 EOF 3=第 N 次后读错误 */
    int fail_after;
    uint32_t maxes[8];
    int max_count;
} fota_src_t;

static int src_read(uint8_t *buf, uint32_t max, void *user)
{
    fota_src_t *s = (fota_src_t *)user;
    uint32_t n;

    s->calls++;
    if (s->max_count < (int)(sizeof(s->maxes) / sizeof(s->maxes[0]))) {
        s->maxes[s->max_count++] = max;
    }

    if (s->mode == 1) {
        return -1;
    }
    if (s->mode == 2) {
        return 0;
    }
    if (s->mode == 3 && s->calls > s->fail_after) {
        return -1;
    }
    if (s->pos >= s->total) {
        return 0;
    }

    n = s->total - s->pos;
    if (n > max) {
        n = max;
    }
    memcpy(buf, s->data + s->pos, n);
    s->pos += n;
    return (int)n;
}

static void apply_hook(void)
{
    s_apply_calls++;
}

static void src_reset(fota_src_t *s, int mode, int fail_after)
{
    memset(s, 0, sizeof(*s));
    s->data = s_fw;
    s->total = FW_SIZE;
    s->mode = mode;
    s->fail_after = fail_after;
}

void ut_fota(void)
{
    partition_t part;
    upgrade_status_t st;
    fota_src_t src;
    uint8_t check[FW_SIZE];
    uint32_t i;

    for (i = 0; i < FW_SIZE; i++) {
        s_fw[i] = (uint8_t)(i & 0xFFu);
    }

    tf_begin("fota/OTA 分区准备");
    if (partition_get(PART_OTA) == NULL) {
        part.type = PART_OTA;
        part.address = OTA_ADDR;
        part.size = OTA_SIZE;
        part.erase_size = 256;
        part.writable = true;
        TF_CHECK_INT(partition_register(&part), 0);
    }
    TF_CHECK(partition_get(PART_OTA) != NULL);
    tf_end();

    tf_begin("fota/初始化清空回调与参数校验");
    TF_CHECK_INT(fota_init(), 0);
    /* 未设置读取源时直接失败 */
    TF_CHECK_INT(fota_run(1, 100), -1);
    TF_CHECK_INT(fota_set_read(NULL, NULL), -1);
    TF_CHECK_INT(fota_set_apply(NULL), 0);
    TF_CHECK_INT(upgrade_get_status(&st), 0);
    TF_CHECK_INT(st.state, UPGRADE_IDLE);
    tf_end();

    tf_begin("fota/设置读取源与非法长度");
    src_reset(&src, 0, 0);
    TF_CHECK_INT(fota_set_read(src_read, &src), 0);
    TF_CHECK_INT(fota_run(1, 0), -1);      /* size 为 0 不进入升级 */
    TF_CHECK_INT(src.calls, 0);
    tf_end();

    tf_begin("fota/成功升级：分片写入与完成");
    s_apply_calls = 0;
    src_reset(&src, 0, 0);
    memset(check, 0, sizeof(check));

    TF_CHECK_INT(fota_set_apply(apply_hook), 0);
    TF_CHECK_INT(fota_run(0x01020304u, FW_SIZE), 0);

    /* 600 字节按 256 + 256 + 88 分片读取 */
    TF_CHECK_INT(src.calls, 3);
    TF_CHECK_INT(src.max_count, 3);
    if (src.max_count == 3) {
        TF_CHECK_INT(src.maxes[0], 256);
        TF_CHECK_INT(src.maxes[1], 256);
        TF_CHECK_INT(src.maxes[2], 88);
    }
    TF_CHECK_INT(s_apply_calls, 1);

    TF_CHECK_INT(upgrade_get_status(&st), 0);
    TF_CHECK_INT(st.state, UPGRADE_SUCCESS);
    TF_CHECK_INT(st.total_size, FW_SIZE);
    TF_CHECK_INT(st.received_size, FW_SIZE);
    TF_CHECK_INT(st.error, 0);

    /* 分片内容已按偏移写入 OTA 分区 */
    TF_CHECK_INT(partition_read(PART_OTA, 0, check, sizeof(check)), 0);
    TF_CHECK_MEM(check, s_fw, sizeof(s_fw));
    tf_end();

    tf_begin("fota/重复开始一次升级");
    src_reset(&src, 0, 0);
    TF_CHECK_INT(fota_run(0x01020305u, FW_SIZE), 0);
    TF_CHECK_INT(s_apply_calls, 2);
    TF_CHECK_INT(upgrade_get_status(&st), 0);
    TF_CHECK_INT(st.state, UPGRADE_SUCCESS);
    tf_end();

    tf_begin("fota/读取错误中止升级");
    src_reset(&src, 3, 1);     /* 第一片成功，第二片读失败 */
    TF_CHECK_INT(fota_run(1, FW_SIZE), -1);
    TF_CHECK_INT(src.calls, 2);
    TF_CHECK_INT(s_apply_calls, 2);
    TF_CHECK_INT(upgrade_get_status(&st), 0);
    TF_CHECK_INT(st.state, UPGRADE_IDLE);   /* upgrade_abort 复位为 IDLE */
    tf_end();

    tf_begin("fota/立即读错误中止升级");
    src_reset(&src, 1, 0);
    TF_CHECK_INT(fota_run(1, FW_SIZE), -1);
    TF_CHECK_INT(src.calls, 1);
    TF_CHECK_INT(upgrade_get_status(&st), 0);
    TF_CHECK_INT(st.state, UPGRADE_IDLE);
    tf_end();

    tf_begin("fota/数据不足导致校验失败");
    src_reset(&src, 2, 0);     /* 读回调立即返回 0，接收长度小于声明长度 */
    TF_CHECK_INT(fota_run(1, FW_SIZE), -1);
    TF_CHECK_INT(upgrade_get_status(&st), 0);
    TF_CHECK_INT(st.state, UPGRADE_FAILED);
    TF_CHECK_INT(st.received_size, 0);
    tf_end();

    tf_begin("fota/超过 OTA 分区容量");
    TF_CHECK_INT(fota_init(), 0);
    src_reset(&src, 0, 0);
    TF_CHECK_INT(fota_set_read(src_read, &src), 0);
    TF_CHECK_INT(fota_run(1, OTA_SIZE + 1), -1);
    TF_CHECK_INT(src.calls, 0);
    TF_CHECK_INT(upgrade_get_status(&st), 0);
    TF_CHECK_INT(st.state, UPGRADE_IDLE);
    tf_end();

    tf_begin("fota/失败后可重新开始并成功");
    src_reset(&src, 0, 0);
    TF_CHECK_INT(fota_run(2, FW_SIZE), 0);
    TF_CHECK_INT(upgrade_get_status(&st), 0);
    TF_CHECK_INT(st.state, UPGRADE_SUCCESS);
    tf_end();

    tf_begin("fota/无 apply 钩子时请求 Bootloader 升级");
    TF_CHECK_INT(fota_init(), 0);   /* 清空 apply 钩子 */
    src_reset(&src, 0, 0);
    src.total = 100;
    TF_CHECK_INT(fota_set_read(src_read, &src), 0);
    TF_CHECK_INT(boot_init(), 0);
    TF_CHECK_INT(fota_run(3, 100), 0);
    TF_CHECK_INT(boot_select_image(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_UPGRADE);
    tf_end();
}
