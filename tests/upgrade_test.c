/* boot/upgrade.c per-file 单元测试。
 * 说明：upgrade 依赖 PART_OTA 分区（partition.c 的 RAM 模拟 Flash），
 * 分区表为进程级静态状态，统一用 ensure_partitions() 幂等补齐。 */
#include "test_framework.h"
#include "upgrade.h"
#include "partition.h"

#include <string.h>

static const partition_t s_layout[] = {
    { PART_BOOT,       0x0000u, 0x2000u, 0x400u, false },
    { PART_APP,        0x2000u, 0x4000u, 0x400u, true  },
    { PART_APP_BACKUP, 0x6000u, 0x4000u, 0x400u, true  },
    { PART_OTA,        0xA000u, 0x4000u, 0x400u, true  },
    { PART_CONFIG,     0xE000u, 0x1000u, 0x400u, true  },
    { PART_LOG,        0xF000u, 0x1000u, 0x400u, true  }
};

static void ensure_partitions(void)
{
    size_t i;
    for (i = 0; i < sizeof(s_layout) / sizeof(s_layout[0]); i++) {
        if (partition_get(s_layout[i].type) == NULL) {
            (void)partition_register(&s_layout[i]);
        }
    }
}

static int s_cb_calls = 0;
static int s_user_token = 0;
static upgrade_state_t s_cb_state = UPGRADE_IDLE;
static uint32_t s_cb_total = 0;
static uint32_t s_cb_received = 0;
static int s_cb_user_ok = 0;

static void progress_cb(const upgrade_status_t *status, void *user)
{
    s_cb_calls++;
    s_cb_state = status->state;
    s_cb_total = status->total_size;
    s_cb_received = status->received_size;
    if (user == &s_user_token) {
        s_cb_user_ok = 1;
    }
}

static void reset_cb_stats(void)
{
    s_cb_calls = 0;
    s_cb_state = UPGRADE_IDLE;
    s_cb_total = 0;
    s_cb_received = 0;
    s_cb_user_ok = 0;
}

static void fill(uint8_t *buf, uint32_t len, uint8_t seed)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        buf[i] = (uint8_t)(seed + i);
    }
}

void ut_upgrade(void)
{
    upgrade_info_t info;
    upgrade_status_t status;
    uint8_t data[256];
    uint8_t readback[256];
    const partition_t *ota;

    ensure_partitions();
    ota = partition_get(PART_OTA);
    TF_CHECK(ota != NULL);
    if (ota == NULL) {
        return;
    }

    memset(&info, 0, sizeof(info));
    info.version = 0x00010002u;
    info.size = 256u;
    info.hardware_id = 1u;
    info.product_id = 2u;
    fill(data, sizeof(data), 0x10u);

    tf_begin("upgrade 初始化与状态复位");
    TF_CHECK_INT(upgrade_init(), IOT_OK);
    memset(&status, 0xFF, sizeof(status));
    TF_CHECK_INT(upgrade_get_status(&status), IOT_OK);
    TF_CHECK_INT(status.state, UPGRADE_IDLE);
    TF_CHECK_INT(status.total_size, 0);
    TF_CHECK_INT(status.received_size, 0);
    TF_CHECK_INT(status.current_offset, 0);
    TF_CHECK_INT(status.error, 0);
    TF_CHECK_INT(upgrade_get_status(NULL), -1);
    tf_end();

    tf_begin("upgrade begin 参数校验");
    TF_CHECK_INT(upgrade_begin(NULL), -1);

    info.size = 0;
    TF_CHECK_INT(upgrade_begin(&info), -1);

    info.size = ota->size + 1u;      /* 超过 OTA 分区容量 */
    TF_CHECK_INT(upgrade_begin(&info), -1);

    info.size = 256u;
    TF_CHECK_INT(upgrade_begin(&info), IOT_OK);
    TF_CHECK_INT(upgrade_get_status(&status), IOT_OK);
    TF_CHECK_INT(status.state, UPGRADE_RECEIVING);
    TF_CHECK_INT(status.total_size, 256);
    TF_CHECK_INT(status.received_size, 0);
    tf_end();

    tf_begin("upgrade write 参数校验");
    TF_CHECK_INT(upgrade_write(0, NULL, 16), -1);
    TF_CHECK_INT(upgrade_write(0, data, 0), -1);
    TF_CHECK_INT(upgrade_write(250, data, 10), -1);   /* offset + len 越界 */

    TF_CHECK_INT(upgrade_write(0, data, 128), IOT_OK);
    TF_CHECK_INT(upgrade_get_status(&status), IOT_OK);
    TF_CHECK_INT(status.received_size, 128);
    TF_CHECK_INT(status.current_offset, 128);

    TF_CHECK_INT(upgrade_write(128, data + 128, 128), IOT_OK);
    TF_CHECK_INT(upgrade_get_status(&status), IOT_OK);
    TF_CHECK_INT(status.received_size, 256);

    /* 数据确实写入 OTA 分区 */
    memset(readback, 0, sizeof(readback));
    TF_CHECK_INT(partition_read(PART_OTA, 0, readback, 256), IOT_OK);
    TF_CHECK_MEM(readback, data, 256);

    /* 未开始（IDLE）时写入应失败 */
    TF_CHECK_INT(upgrade_init(), IOT_OK);
    TF_CHECK_INT(upgrade_write(0, data, 1), -1);
    tf_end();

    tf_begin("upgrade 乱序写入与校验失败");
    TF_CHECK_INT(upgrade_begin(&info), IOT_OK);
    TF_CHECK_INT(upgrade_write(100, data, 50), IOT_OK);
    TF_CHECK_INT(upgrade_get_status(&status), IOT_OK);
    TF_CHECK_INT(status.received_size, 150);

    /* 已覆盖范围的低偏移写入不增加 received_size */
    TF_CHECK_INT(upgrade_write(0, data, 100), IOT_OK);
    TF_CHECK_INT(upgrade_get_status(&status), IOT_OK);
    TF_CHECK_INT(status.received_size, 150);

    /* 数据不足导致校验失败 */
    TF_CHECK_INT(upgrade_verify(), -1);
    TF_CHECK_INT(upgrade_get_status(&status), IOT_OK);
    TF_CHECK_INT(status.state, UPGRADE_FAILED);
    TF_CHECK_INT(upgrade_write(0, data, 1), -1);
    tf_end();

    tf_begin("upgrade 完整流程与进度回调");
    TF_CHECK_INT(upgrade_init(), IOT_OK);
    reset_cb_stats();
    TF_CHECK_INT(upgrade_set_callback(progress_cb, &s_user_token), IOT_OK);
    TF_CHECK_INT(upgrade_begin(&info), IOT_OK);
    TF_CHECK_INT(s_cb_calls, 1);
    TF_CHECK_INT(s_cb_state, UPGRADE_RECEIVING);
    TF_CHECK_INT(s_cb_total, 256);
    TF_CHECK_INT(s_cb_received, 0);
    TF_CHECK_INT(s_cb_user_ok, 1);

    TF_CHECK_INT(upgrade_write(0, data, 64), IOT_OK);
    TF_CHECK_INT(s_cb_calls, 2);
    TF_CHECK_INT(s_cb_received, 64);
    TF_CHECK_INT(upgrade_write(64, data, 192), IOT_OK);
    TF_CHECK_INT(s_cb_calls, 3);
    TF_CHECK_INT(s_cb_received, 256);

    TF_CHECK_INT(upgrade_verify(), IOT_OK);
    TF_CHECK_INT(upgrade_get_status(&status), IOT_OK);
    TF_CHECK_INT(status.state, UPGRADE_READY);

    TF_CHECK_INT(upgrade_finish(), IOT_OK);
    TF_CHECK_INT(s_cb_calls, 4);
    TF_CHECK_INT(s_cb_state, UPGRADE_SUCCESS);
    TF_CHECK_INT(upgrade_get_status(&status), IOT_OK);
    TF_CHECK_INT(status.state, UPGRADE_SUCCESS);

    /* 已完成不能再 finish */
    TF_CHECK_INT(upgrade_finish(), -1);
    tf_end();

    tf_begin("upgrade resume / abort / 错误顺序");
    TF_CHECK_INT(upgrade_init(), IOT_OK);
    TF_CHECK_INT(upgrade_verify(), -1);          /* IDLE 不可校验 */
    TF_CHECK_INT(upgrade_finish(), -1);          /* IDLE 不可完成 */

    TF_CHECK_INT(upgrade_resume(), IOT_OK);
    TF_CHECK_INT(upgrade_get_status(&status), IOT_OK);
    TF_CHECK_INT(status.state, UPGRADE_RECEIVING);

    TF_CHECK_INT(upgrade_abort(), IOT_OK);
    TF_CHECK_INT(upgrade_get_status(&status), IOT_OK);
    TF_CHECK_INT(status.state, UPGRADE_IDLE);

    /* abort 后仍可重新发起 */
    TF_CHECK_INT(upgrade_begin(&info), IOT_OK);
    TF_CHECK_INT(upgrade_get_status(&status), IOT_OK);
    TF_CHECK_INT(status.state, UPGRADE_RECEIVING);
    TF_CHECK_INT(status.total_size, 256);
    TF_CHECK_INT(upgrade_abort(), IOT_OK);
    tf_end();

    tf_begin("upgrade 重复初始化清除回调");
    reset_cb_stats();
    TF_CHECK_INT(upgrade_set_callback(progress_cb, &s_user_token), IOT_OK);
    TF_CHECK_INT(upgrade_init(), IOT_OK);        /* 清除回调 */
    TF_CHECK_INT(upgrade_begin(&info), IOT_OK);
    TF_CHECK_INT(s_cb_calls, 0);                 /* 未再触发回调 */
    TF_CHECK_INT(upgrade_set_callback(NULL, NULL), IOT_OK);
    TF_CHECK_INT(upgrade_write(0, data, 10), IOT_OK);
    TF_CHECK_INT(s_cb_calls, 0);
    tf_end();
}
