/*
 * partition_test.c - 分区表测试（对应 partition.c）
 *
 * 覆盖：注册与查询、越界/零大小拒绝、分区内读写擦除、
 *       未注册分区与越界访问。
 */

#include "test.h"
#include "partition.h"

void ut_partition(void)
{
    partition_t boot;
    partition_t app;
    partition_t ota;
    uint8_t buf[16];
    const uint8_t data[8] = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U };

    tf_begin("partition/注册与查询");

    TF_CHECK_INT(partition_register(NULL), -1);

    memset(&boot, 0, sizeof(boot));
    boot.type = PART_BOOT;
    boot.address = 0x0000U;
    boot.size = 0x1000U;
    boot.erase_size = 0x400U;
    boot.writable = false;
    TF_CHECK_INT(partition_register(&boot), 0);

    memset(&app, 0, sizeof(app));
    app.type = PART_APP;
    app.address = 0x1000U;
    app.size = 0x4000U;
    app.erase_size = 0x400U;
    app.writable = true;
    TF_CHECK_INT(partition_register(&app), 0);

    memset(&ota, 0, sizeof(ota));
    ota.type = PART_OTA;
    ota.address = 0x5000U;
    ota.size = 0x4000U;
    ota.erase_size = 0x400U;
    ota.writable = true;
    TF_CHECK_INT(partition_register(&ota), 0);

    TF_CHECK_INT(partition_get(PART_APP)->address, 0x1000);
    TF_CHECK(partition_get(PART_OTA) != NULL);
    TF_CHECK(partition_get(PART_CONFIG) == NULL);

    /* 零大小与越界 */
    {
        partition_t bad;

        memset(&bad, 0, sizeof(bad));
        bad.type = PART_CONFIG;
        bad.address = 0x0000U;
        bad.size = 0U;
        TF_CHECK_INT(partition_register(&bad), -1);

        bad.size = 0x2000U;
        bad.address = 0xF000U;            /* 超过模拟 Flash 大小 */
        TF_CHECK_INT(partition_register(&bad), -1);
    }

    tf_end();

    tf_begin("partition/读写与擦除");

    TF_CHECK_INT(partition_write(PART_APP, 0U, data, sizeof(data)), 0);
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(partition_read(PART_APP, 0U, buf, sizeof(data)), 0);
    TF_CHECK_MEM(buf, data, sizeof(data));

    TF_CHECK_INT(partition_erase(PART_APP, 0U, 0x400U), 0);
    TF_CHECK_INT(partition_read(PART_APP, 0U, buf, 1U), 0);

    /* 越界与非法参数 */
    TF_CHECK_INT(partition_read(PART_APP, 0x4000U, buf, 1U), -1);
    TF_CHECK_INT(partition_write(PART_APP, 0U, NULL, 1U), -1);
    TF_CHECK_INT(partition_read(PART_CONFIG, 0U, buf, 1U), -1);
    TF_CHECK_INT(partition_write(PART_CONFIG, 0U, data, 1U), -1);
    TF_CHECK_INT(partition_erase(PART_CONFIG, 0U, 1U), -1);

    tf_end();
}
