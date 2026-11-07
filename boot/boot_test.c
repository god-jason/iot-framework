/* boot/boot.c per-file 单元测试。
 * 说明：boot_verify_image / boot_install_image 依赖 Flash，这里使用 partition.c 的
 * RAM 模拟 Flash 验证；真正的向量表切换与跳转由 boot_set_jump 钩子桩替代。
 * 分区表为进程级静态状态且无注销接口，因此统一用 ensure_partitions() 只补缺失项，
 * 与 partition/upgrade 测试共享同一布局。 */
#include "test.h"
#include "boot.h"
#include "image.h"
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

static int s_jump_calls = 0;

static void jump_stub(void)
{
    s_jump_calls++;
}

static void write_ota_header(uint32_t magic, uint32_t header_size, uint32_t image_size)
{
    image_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = magic;
    header.header_size = header_size;
    header.product_id = 0x1234u;
    header.version = 0x01020304u;
    header.image_size = image_size;
    partition_write(PART_OTA, 0, &header, (uint32_t)sizeof(header));
}

void ut_boot(void)
{
    image_state_t state;
    uint8_t block[64];
    uint8_t readback[64];
    uint32_t off;
    uint32_t i;

    ensure_partitions();

    tf_begin("boot 初始化与模式复位");
    TF_CHECK_INT(boot_init(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_NORMAL);
    TF_CHECK_INT(boot_select_image(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_NORMAL);

    /* 重复初始化应清空所有请求标志 */
    TF_CHECK_INT(boot_request_upgrade(), 0);
    TF_CHECK_INT(boot_request_rollback(), 0);
    TF_CHECK_INT(boot_init(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_NORMAL);
    TF_CHECK_INT(boot_select_image(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_NORMAL);
    tf_end();

    tf_begin("boot 升级/回滚请求与优先级");
    TF_CHECK_INT(boot_request_upgrade(), 0);
    TF_CHECK_INT(boot_select_image(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_UPGRADE);

    /* 回滚优先级高于升级 */
    TF_CHECK_INT(boot_request_rollback(), 0);
    TF_CHECK_INT(boot_select_image(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_ROLLBACK);

    /* 确认应用后两个请求都被清除 */
    TF_CHECK_INT(boot_confirm_application(), 0);
    TF_CHECK_INT(boot_select_image(), 0);
    TF_CHECK_INT(boot_get_mode(), BOOT_NORMAL);
    tf_end();

    tf_begin("boot 镜像校验");
    /* 魔数错误 */
    write_ota_header(0xDEADBEEFu, (uint32_t)sizeof(image_header_t), 0x800u);
    TF_CHECK_INT(boot_verify_image(), -1);
    TF_CHECK_INT(image_get_state(&state), 0);
    TF_CHECK_INT(state, IMAGE_INVALID);

    /* 头部长度不足 */
    write_ota_header(IMAGE_MAGIC, (uint32_t)sizeof(image_header_t) - 1u, 0x800u);
    TF_CHECK_INT(boot_verify_image(), -1);

    /* 镜像长度为 0 */
    write_ota_header(IMAGE_MAGIC, (uint32_t)sizeof(image_header_t), 0u);
    TF_CHECK_INT(boot_verify_image(), -1);

    /* 合法头 */
    write_ota_header(IMAGE_MAGIC, (uint32_t)sizeof(image_header_t), 0x800u);
    TF_CHECK_INT(boot_verify_image(), 0);
    TF_CHECK_INT(image_get_state(&state), 0);
    TF_CHECK_INT(state, IMAGE_VALID);
    tf_end();

    tf_begin("boot 安装镜像 OTA -> APP");
    {
        const partition_t *ota = partition_get(PART_OTA);
        TF_CHECK(ota != NULL);
        TF_CHECK_INT(boot_install_image(), 0);
        if (ota != NULL) {
            /* 逐个 64 字节块写入 OTA，再安装并抽查首/中/尾块 */
            for (off = 0; off < ota->size; off += sizeof(block)) {
                uint32_t n = (ota->size - off > sizeof(block))
                                 ? (uint32_t)sizeof(block)
                                 : (ota->size - off);
                for (i = 0; i < n; i++) {
                    block[i] = (uint8_t)((off + i) & 0xFFu);
                }
                TF_CHECK_INT(partition_write(PART_OTA, off, block, n), 0);
            }
            TF_CHECK_INT(boot_install_image(), 0);

            memset(readback, 0, sizeof(readback));
            TF_CHECK_INT(partition_read(PART_APP, 0, readback, sizeof(readback)), 0);
            for (i = 0; i < sizeof(readback); i++) {
                TF_CHECK_INT(readback[i], (uint8_t)(i & 0xFFu));
            }

            off = ota->size / 2u;
            TF_CHECK_INT(partition_read(PART_APP, off, readback, sizeof(readback)), 0);
            for (i = 0; i < sizeof(readback); i++) {
                TF_CHECK_INT(readback[i], (uint8_t)((off + i) & 0xFFu));
            }

            off = ota->size - sizeof(block);
            TF_CHECK_INT(partition_read(PART_APP, off, readback, sizeof(readback)), 0);
            for (i = 0; i < sizeof(readback); i++) {
                TF_CHECK_INT(readback[i], (uint8_t)((off + i) & 0xFFu));
            }
        }
    }
    tf_end();

    tf_begin("boot 启动应用与跳转钩子");
    TF_CHECK_INT(boot_set_jump(NULL), 0);
    TF_CHECK_INT(boot_start_application(), -1);

    s_jump_calls = 0;
    TF_CHECK_INT(boot_set_jump(jump_stub), 0);
    TF_CHECK_INT(boot_start_application(), 0);
    TF_CHECK_INT(s_jump_calls, 1);
    TF_CHECK_INT(boot_start_application(), 0);
    TF_CHECK_INT(s_jump_calls, 2);

    /* 复位钩子后又应返回失败 */
    TF_CHECK_INT(boot_set_jump(NULL), 0);
    TF_CHECK_INT(boot_start_application(), -1);
    TF_CHECK_INT(s_jump_calls, 2);
    tf_end();
}
