/* boot/partition.c per-file 单元测试。
 * 说明：分区表是进程级静态状态且没有注销/复位接口，因此测试只做“幂等补齐”：
 * 已注册的类型不重复注册，避免影响 boot/upgrade 测试。容量上限测试保留在最后，
 * 此时六种类型均已注册，后续测试仍可查询到它们。 */
#include "test_framework.h"
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

void ut_partition(void)
{
    uint8_t buf[16];
    uint8_t data[8] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 0 };
    partition_t extra;
    int i;

    ensure_partitions();

    tf_begin("partition 注册与查询");
    for (i = 0; i < 6; i++) {
        const partition_t *p = partition_get(s_layout[i].type);
        TF_CHECK(p != NULL);
        if (p != NULL) {
            TF_CHECK_INT(p->type, s_layout[i].type);
            TF_CHECK_INT(p->address, s_layout[i].address);
            TF_CHECK_INT(p->size, s_layout[i].size);
            TF_CHECK_INT(p->erase_size, s_layout[i].erase_size);
        }
    }
    TF_CHECK(partition_get((partition_type_t)99) == NULL);
    tf_end();

    tf_begin("partition 读写与擦除");
    TF_CHECK_INT(partition_write(PART_CONFIG, 0x10u, data, 7), IOT_OK);
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(partition_read(PART_CONFIG, 0x10u, buf, 7), IOT_OK);
    TF_CHECK_MEM(buf, data, 7);

    /* 只读分区可读 */
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(partition_read(PART_BOOT, 0, buf, sizeof(buf)), IOT_OK);

    /* 擦除后为 0xFF */
    TF_CHECK_INT(partition_erase(PART_CONFIG, 0x10u, 7), IOT_OK);
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(partition_read(PART_CONFIG, 0x10u, buf, 7), IOT_OK);
    for (i = 0; i < 7; i++) {
        TF_CHECK_INT(buf[i], 0xFF);
    }

    /* 跨多次写读一致 */
    TF_CHECK_INT(partition_write(PART_CONFIG, 0x20u, data, 8), IOT_OK);
    TF_CHECK_INT(partition_write(PART_CONFIG, 0x28u, data, 8), IOT_OK);
    memset(buf, 0, sizeof(buf));
    TF_CHECK_INT(partition_read(PART_CONFIG, 0x20u, buf, 16), IOT_OK);
    TF_CHECK_MEM(buf, data, 8);
    tf_end();

    tf_begin("partition 参数错误路径");
    TF_CHECK_INT(partition_register(NULL), -1);

    extra = s_layout[4];
    extra.size = 0;
    TF_CHECK_INT(partition_register(&extra), -1);

    extra = s_layout[4];
    extra.address = 0xF800u;
    extra.size = 0x1000u;   /* 0xF800 + 0x1000 > 0x10000 */
    TF_CHECK_INT(partition_register(&extra), -1);

    TF_CHECK_INT(partition_read((partition_type_t)99, 0, buf, 1), -1);
    TF_CHECK_INT(partition_write((partition_type_t)99, 0, data, 1), -1);
    TF_CHECK_INT(partition_erase((partition_type_t)99, 0, 1), -1);

    TF_CHECK_INT(partition_read(PART_CONFIG, 0, NULL, 1), -1);
    TF_CHECK_INT(partition_write(PART_CONFIG, 0, NULL, 1), -1);

    TF_CHECK_INT(partition_read(PART_CONFIG, 0xFFFu, buf, 2), -1);
    TF_CHECK_INT(partition_write(PART_CONFIG, 0xFFFu, data, 2), -1);
    TF_CHECK_INT(partition_erase(PART_CONFIG, 0xFFFu, 2), -1);

    /* 只读分区不可写 */
    TF_CHECK_INT(partition_write(PART_BOOT, 0, data, 1), -1);

    /* 长度 0 的读写擦除是合法的 */
    TF_CHECK_INT(partition_read(PART_CONFIG, 0, buf, 0), IOT_OK);
    TF_CHECK_INT(partition_write(PART_CONFIG, 0, data, 0), IOT_OK);
    TF_CHECK_INT(partition_erase(PART_CONFIG, 0, 0), IOT_OK);
    tf_end();

    tf_begin("partition 重复注册与容量上限");
    /* 重复注册同类型不覆盖已存在的第一项 */
    extra = s_layout[4];
    extra.address = 0xF800u;
    extra.size = 0x800u;
    (void)partition_register(&extra);
    TF_CHECK_INT(partition_get(PART_CONFIG)->address, s_layout[4].address);

    /* 填满 8 个表项后继续注册应失败 */
    extra = s_layout[5];
    for (i = 0; i < 16; i++) {
        if (partition_register(&extra) != IOT_OK) {
            break;
        }
    }
    TF_CHECK_INT(partition_register(&extra), -1);
    TF_CHECK_INT(partition_register(&extra), -1);

    /* 表满后所有既有类型仍可查询到 */
    for (i = 0; i < 6; i++) {
        TF_CHECK(partition_get(s_layout[i].type) != NULL);
    }
    TF_CHECK_INT(partition_get(PART_APP)->size, s_layout[1].size);
    tf_end();
}
