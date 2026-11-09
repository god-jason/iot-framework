/*
 * upgrade_test.c - 升级管理器测试（对应 upgrade.c）
 *
 * 覆盖：初始化、开始（依赖 OTA 分区与容量）、分块写入与进度回调、
 *       越界写入、未接收满时校验失败、完成、状态与中止。
 */

#include "test.h"
#include "upgrade.h"
#include "partition.h"

static int s_progress_calls;
static upgrade_state_t s_last_state;

static void on_progress(const upgrade_status_t *status, void *user)
{
    (void)user;
    s_progress_calls++;
    s_last_state = status->state;
}

void ut_upgrade(void)
{
    partition_t ota;
    const partition_t *p;
    upgrade_info_t info;
    upgrade_status_t status;
    uint32_t part_size;
    const uint8_t payload[8] = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U };

    tf_begin("upgrade/初始化与开始");

    TF_CHECK_INT(upgrade_init(), 0);
    TF_CHECK_INT(upgrade_get_status(&status), 0);
    TF_CHECK_INT(status.state, UPGRADE_IDLE);
    TF_CHECK_INT(upgrade_get_status(NULL), -1);

    memset(&ota, 0, sizeof(ota));
    ota.type = PART_OTA;
    ota.address = 0x5000U;
    ota.size = 0x1000U;
    ota.erase_size = 0x400U;
    ota.writable = true;
    TF_CHECK_INT(partition_register(&ota), 0);

    p = partition_get(PART_OTA);
    TF_CHECK(p != NULL);
    part_size = p->size;

    TF_CHECK_INT(upgrade_begin(NULL), -1);
    info.size = 0U;
    TF_CHECK_INT(upgrade_begin(&info), -1);

    info.size = part_size + 1U;                /* 超过分区容量 */
    TF_CHECK_INT(upgrade_begin(&info), -1);

    info.size = sizeof(payload);
    info.version = 0x0100U;
    TF_CHECK_INT(upgrade_begin(&info), 0);

    TF_CHECK_INT(upgrade_get_status(&status), 0);
    TF_CHECK_INT(status.state, UPGRADE_RECEIVING);
    TF_CHECK_INT(status.total_size, (int)sizeof(payload));

    tf_end();

    tf_begin("upgrade/写入与进度");

    s_progress_calls = 0;
    TF_CHECK_INT(upgrade_set_callback(on_progress, NULL), 0);

    TF_CHECK_INT(upgrade_write(0U, payload, sizeof(payload)), 0);
    TF_CHECK_INT(upgrade_get_status(&status), 0);
    TF_CHECK_INT(status.received_size, (int)sizeof(payload));
    TF_CHECK_INT(s_progress_calls, 1);
    TF_CHECK_INT(s_last_state, UPGRADE_RECEIVING);

    /* 参数与越界 */
    TF_CHECK_INT(upgrade_write(0U, NULL, 1U), -1);
    TF_CHECK_INT(upgrade_write(0U, payload, 0U), -1);
    TF_CHECK_INT(upgrade_write((uint32_t)sizeof(payload), payload, 1U), -1);

    tf_end();

    tf_begin("upgrade/校验与完成");

    TF_CHECK_INT(upgrade_verify(), 0);
    TF_CHECK_INT(upgrade_get_status(&status), 0);
    TF_CHECK_INT(status.state, UPGRADE_READY);

    s_progress_calls = 0;
    TF_CHECK_INT(upgrade_finish(), 0);
    TF_CHECK_INT(upgrade_get_status(&status), 0);
    TF_CHECK_INT(status.state, UPGRADE_SUCCESS);
    TF_CHECK_INT(s_progress_calls, 1);

    /* 已结束后重复完成失败 */
    TF_CHECK_INT(upgrade_finish(), -1);

    tf_end();

    tf_begin("upgrade/未接收满导致校验失败");

    info.size = 16U;
    TF_CHECK_INT(upgrade_begin(&info), 0);
    TF_CHECK_INT(upgrade_write(0U, payload, sizeof(payload)), 0);
    TF_CHECK_INT(upgrade_verify(), -1);
    TF_CHECK_INT(upgrade_get_status(&status), 0);
    TF_CHECK_INT(status.state, UPGRADE_FAILED);

    tf_end();

    tf_begin("upgrade/中止与工具接口");

    TF_CHECK_INT(upgrade_abort(), 0);
    TF_CHECK_INT(upgrade_get_status(&status), 0);
    TF_CHECK_INT(status.state, UPGRADE_IDLE);

    /* 非接收状态写入失败 */
    TF_CHECK_INT(upgrade_write(0U, payload, 1U), -1);
    TF_CHECK_INT(upgrade_resume(), 0);
    TF_CHECK_INT(upgrade_get_status(&status), 0);
    TF_CHECK_INT(status.state, UPGRADE_RECEIVING);

    TF_CHECK_INT(upgrade_set_callback(NULL, NULL), 0);

    tf_end();
}
