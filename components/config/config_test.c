/*
 * config_test.c - 基于 kvfs 的配置存储测试（单例接口）
 *
 * 用 RAM 模拟 Flash（编程只能 1→0、擦除按扇区），覆盖：
 *   空设备、保存/读取/长度校验、重新挂载、写满压缩、删除与再写。
 */

#include "test.h"
#include "config.h"

#include <string.h>

#define T_ERASE     1024U
#define T_TOTAL     (T_ERASE * 2U)
#define T_MAX_VALUE 64U
#define T_KEY       0x43464731U     /* 'CFG1' */
#define T_VERSION   1U

typedef struct {
    uint32_t value;
    uint16_t tag;
    uint16_t pad;
} sample_t;

static uint8_t s_flash[T_TOTAL];
static uint32_t s_erase_count;
static uint8_t s_work[T_MAX_VALUE];
static kvfs_t s_kv;

/* ============================== RAM Flash ============================== */

static int t_read(void *ctx, uint32_t addr, void *buf, uint32_t size)
{
    (void)ctx;
    if (addr > T_TOTAL || size > T_TOTAL - addr) {
        return -1;
    }
    memcpy(buf, &s_flash[addr], size);
    return 0;
}

static int t_program(void *ctx, uint32_t addr, const void *buf, uint32_t size)
{
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t i;

    (void)ctx;
    if (addr > T_TOTAL || size > T_TOTAL - addr) {
        return -1;
    }
    for (i = 0U; i < size; i++) {
        s_flash[addr + i] &= src[i];       /* Flash：编程只能把 1 变成 0 */
    }
    return 0;
}

static int t_erase(void *ctx, uint32_t addr, uint32_t size)
{
    (void)ctx;
    if (size != T_ERASE || (addr % T_ERASE) != 0U ||
        addr > T_TOTAL || size > T_TOTAL - addr) {
        return -1;
    }
    memset(&s_flash[addr], 0xFF, size);
    s_erase_count++;
    return 0;
}

static const kvfs_blockdev_t s_dev = {
    .read = t_read,
    .program = t_program,
    .erase = t_erase,
    .sync = NULL,
    .ctx = NULL,
    .start_addr = 0U,
    .total_size = T_TOTAL,
    .erase_size = T_ERASE,
    .program_size = 4U
};

static const kvfs_config_t s_kv_cfg = {
    .workbuf = s_work,
    .workbuf_size = sizeof(s_work),
    .max_value = T_MAX_VALUE,
    .readonly = 0U,
    .lock = NULL,
    .unlock = NULL,
    .mutex_ctx = NULL
};

/* ============================== 辅助 ============================== */

static void store_reset(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_erase_count = 0U;
    memset(&s_kv, 0, sizeof(s_kv));
}

/* 上电：初始化并挂载 KV，擦除态时自动格式化 */
static void kv_open(void)
{
    int rc = kvfs_init(&s_kv, &s_dev, &s_kv_cfg);

    TF_CHECK_INT(rc, KVFS_OK);
    rc = kvfs_mount(&s_kv);
    if (rc == KVFS_ERR_FORMAT) {
        TF_CHECK_INT(kvfs_format(&s_kv), KVFS_OK);
        rc = KVFS_OK;
    }
    TF_CHECK_INT(rc, KVFS_OK);
}

static int cfg_init(void)
{
    return config_init(&s_kv, T_KEY, T_VERSION, T_MAX_VALUE);
}

static int save_value(uint32_t value)
{
    sample_t s;

    memset(&s, 0, sizeof(s));
    s.value = value;
    s.tag = (uint16_t)value;
    return config_save(&s, sizeof(s));
}

static int load_value(uint32_t *value)
{
    sample_t s;
    int rc;

    memset(&s, 0, sizeof(s));
    rc = config_load(&s, sizeof(s));
    if (rc == CONFIG_OK) {
        *value = s.value;
    }
    return rc;
}

void ut_config(void)
{
    uint32_t value = 0U;
    uint32_t i;

    tf_begin("config/空设备");
    store_reset();
    kv_open();
    TF_CHECK_INT(config_init(NULL, T_KEY, T_VERSION, T_MAX_VALUE),
                 CONFIG_ERR_PARAM);
    TF_CHECK_INT(cfg_init(), CONFIG_NONE);
    TF_CHECK_INT(load_value(&value), CONFIG_NONE);
    tf_end();

    tf_begin("config/保存与读取");
    store_reset();
    kv_open();
    {
        uint32_t erase_base = s_erase_count;

        TF_CHECK_INT(cfg_init(), CONFIG_NONE);
        TF_CHECK_INT(save_value(11U), CONFIG_OK);
        TF_CHECK_INT(load_value(&value), CONFIG_OK);
        TF_CHECK_INT(value, 11U);
        TF_CHECK_INT(save_value(22U), CONFIG_OK);
        TF_CHECK_INT(load_value(&value), CONFIG_OK);
        TF_CHECK_INT(value, 22U);
        TF_CHECK_INT(config_check(), CONFIG_OK);
        TF_CHECK_INT(s_erase_count, erase_base);    /* 追加写不擦除 */
    }

    /* 长度不一致 / 超过上限 */
    TF_CHECK_INT(config_load(&value, 4U), CONFIG_ERR_SIZE);
    {
        uint8_t big[T_MAX_VALUE + 4U];

        memset(big, 0, sizeof(big));
        TF_CHECK_INT(config_save(big, sizeof(big)), CONFIG_ERR_SIZE);
    }

    /* 原始读取：输出长度与版本 */
    {
        sample_t raw;
        uint32_t len = 0U;
        uint16_t version = 0U;

        memset(&raw, 0, sizeof(raw));
        TF_CHECK_INT(config_load_raw(&raw, sizeof(raw), &len, &version),
                     CONFIG_OK);
        TF_CHECK_INT((int)len, (int)sizeof(sample_t));
        TF_CHECK_INT(version, T_VERSION);
        TF_CHECK_INT(raw.value, 22U);
    }
    tf_end();

    tf_begin("config/重新挂载");
    store_reset();
    kv_open();
    TF_CHECK_INT(cfg_init(), CONFIG_NONE);
    TF_CHECK_INT(save_value(33U), CONFIG_OK);

    /* 模拟复位：卸载后重新挂载（不擦除），再绑定一次 */
    TF_CHECK_INT(kvfs_unmount(&s_kv), KVFS_OK);
    kv_open();
    TF_CHECK_INT(cfg_init(), CONFIG_OK);
    TF_CHECK_INT(load_value(&value), CONFIG_OK);
    TF_CHECK_INT(value, 33U);
    tf_end();

    tf_begin("config/写满压缩");
    store_reset();
    kv_open();
    TF_CHECK_INT(cfg_init(), CONFIG_NONE);
    for (i = 1U; i <= 80U; i++) {
        TF_CHECK_INT(save_value(i), CONFIG_OK);
    }
    TF_CHECK(s_erase_count > 0U);               /* 分区写满后发生压缩 */
    TF_CHECK_INT(load_value(&value), CONFIG_OK);
    TF_CHECK_INT(value, 80U);
    TF_CHECK_INT(save_value(81U), CONFIG_OK);
    TF_CHECK_INT(load_value(&value), CONFIG_OK);
    TF_CHECK_INT(value, 81U);
    tf_end();

    tf_begin("config/删除与再写");
    store_reset();
    kv_open();
    TF_CHECK_INT(cfg_init(), CONFIG_NONE);
    TF_CHECK_INT(save_value(66U), CONFIG_OK);
    TF_CHECK_INT(config_format(), CONFIG_OK);
    TF_CHECK_INT(load_value(&value), CONFIG_NONE);
    TF_CHECK_INT(config_check(), CONFIG_NONE);
    TF_CHECK_INT(save_value(77U), CONFIG_OK);
    TF_CHECK_INT(load_value(&value), CONFIG_OK);
    TF_CHECK_INT(value, 77U);
    tf_end();
}
