/*
 * fkv_test.c - 双区滚动键值存储测试
 *
 * 用两段 RAM 模拟 Flash（编程只能 1→0、擦除按扇区），覆盖：
 *   空设备、格式化、读写覆盖、删除/墓碑、掉电半条记录、写满轮转、只读、遍历。
 */

#include "test.h"
#include "fkv_internal.h"

#define T_ERASE     1024u
#define T_TOTAL     (T_ERASE * 2u)

static uint8_t s_flash[T_TOTAL];
static uint32_t s_erase_count;
static fkv_t s_kv;
static uint8_t s_work[128];

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

/* ============================== 辅助 ============================== */

static const fkv_blockdev_t s_dev = {
    .read = t_read,
    .program = t_program,
    .erase = t_erase,
    .sync = NULL,
    .ctx = NULL,
    .start_addr = 0U,
    .total_size = T_TOTAL,
    .erase_size = T_ERASE,
    .program_size = 1U
};

static const fkv_config_t s_cfg = {
    .workbuf = s_work,
    .workbuf_size = sizeof(s_work),
    .max_value = 64U,
    .readonly = 0U
};

static void kv_reset(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_erase_count = 0U;
    memset(&s_kv, 0, sizeof(s_kv));
}

static int kv_open(uint8_t readonly)
{
    fkv_config_t cfg = s_cfg;

    cfg.readonly = readonly;
    memset(&s_kv, 0, sizeof(s_kv));
    return fkv_init(&s_kv, &s_dev, &cfg);
}

static int kv_format_open(void)
{
    int rc = kv_open(0U);

    if (rc != FKV_OK) {
        return rc;
    }
    rc = fkv_format(&s_kv);
    if (rc != FKV_OK) {
        return rc;
    }
    return fkv_mount(&s_kv);
}

/* 读取并比较；返回 FKV_OK 表示一致 */
static int expect_value(uint32_t key, const void *want, uint32_t len)
{
    uint8_t buf[64];
    uint32_t got = 0U;
    int rc = fkv_get(&s_kv, key, buf, sizeof(buf), &got);

    if (rc != FKV_OK || got != len || memcmp(buf, want, len) != 0) {
        return -1;
    }
    return FKV_OK;
}

/* 遍历回调：统计存活键数量与键值之和 */
typedef struct {
    uint32_t count;
    uint32_t key_sum;
    uint32_t bytes;
} visit_ctx_t;

static int visit_cb(void *ctx, uint32_t key, const void *value, uint32_t len)
{
    visit_ctx_t *v = (visit_ctx_t *)ctx;

    (void)value;
    v->count++;
    v->key_sum += key;
    v->bytes += len;
    return 0;
}

/* ============================== 测试 ============================== */

void ut_fkv(void)
{
    uint8_t val[16];
    uint32_t len = 0U;
    uint32_t i;

    tf_begin("fkv/未格式化");
    kv_reset();
    TF_CHECK_INT(kv_open(0U), FKV_OK);
    TF_CHECK_INT(fkv_mount(&s_kv), FKV_ERR_FORMAT);
    TF_CHECK_INT(fkv_put(&s_kv, 1U, "x", 1U), FKV_ERR_STATE);
    tf_end();

    tf_begin("fkv/格式化与读写");
    kv_reset();
    TF_CHECK_INT(kv_format_open(), FKV_OK);
    {
        fkv_statfs_t st;

        TF_CHECK_INT(fkv_statfs(&s_kv, &st), FKV_OK);
        TF_CHECK_INT(st.total_bytes, T_ERASE - FKV_SUPER_SIZE);
        TF_CHECK_INT(st.free_bytes, T_ERASE - FKV_SUPER_SIZE);
        TF_CHECK_INT(st.total_blocks, 2U);
    }
    TF_CHECK_INT(fkv_put(&s_kv, 1U, "abc", 3U), FKV_OK);
    TF_CHECK_INT(expect_value(1U, "abc", 3U), FKV_OK);
    TF_CHECK_INT(fkv_size(&s_kv, 1U, &len), FKV_OK);
    TF_CHECK_INT(len, 3U);
    TF_CHECK_INT(fkv_exists(&s_kv, 1U), 1);

    /* 覆盖写：读取返回最新值 */
    TF_CHECK_INT(fkv_put(&s_kv, 1U, "xy", 2U), FKV_OK);
    TF_CHECK_INT(expect_value(1U, "xy", 2U), FKV_OK);

    /* 不存在 / 缓冲不足 / 超长 */
    TF_CHECK_INT(fkv_get(&s_kv, 2U, NULL, 0U, &len), FKV_ERR_NOENT);
    TF_CHECK_INT(fkv_exists(&s_kv, 2U), 0);
    TF_CHECK_INT(fkv_get(&s_kv, 1U, val, 1U, &len), FKV_ERR_NOMEM);
    TF_CHECK_INT(len, 2U);
    {
        uint8_t big[80];

        memset(big, 0x5A, sizeof(big));
        TF_CHECK_INT(fkv_put(&s_kv, 3U, big, sizeof(big)), FKV_ERR_INVAL);
    }
    TF_CHECK_INT(fkv_check(&s_kv), FKV_OK);
    tf_end();

    tf_begin("fkv/删除与墓碑");
    TF_CHECK_INT(fkv_del(&s_kv, 1U), FKV_OK);
    TF_CHECK_INT(fkv_exists(&s_kv, 1U), 0);
    TF_CHECK_INT(fkv_get(&s_kv, 1U, NULL, 0U, NULL), FKV_ERR_NOENT);
    TF_CHECK_INT(fkv_del(&s_kv, 1U), FKV_ERR_NOENT);
    TF_CHECK_INT(fkv_put(&s_kv, 1U, "new", 3U), FKV_OK);
    TF_CHECK_INT(expect_value(1U, "new", 3U), FKV_OK);
    tf_end();

    tf_begin("fkv/掉电未提交记录");
    kv_reset();
    TF_CHECK_INT(kv_format_open(), FKV_OK);
    TF_CHECK_INT(fkv_put(&s_kv, 7U, "hello", 5U), FKV_OK);
    {
        /* 在日志尾部伪造“Header+Payload 已写、Commit 未写”的半成品 */
        fkv_rec_hdr_t hdr;
        uint32_t addr = s_kv.region_base[s_kv.active] + s_kv.write_off;

        memset(&hdr, 0, sizeof(hdr));
        hdr.magic = FKV_REC_MAGIC;
        hdr.version = FKV_VERSION;
        hdr.length = 5U;
        hdr.sequence = 999U;
        hdr.key = 7U;
        hdr.flags = 0U;
        TF_CHECK_INT(t_program(NULL, addr, &hdr, sizeof(hdr)), 0);
        TF_CHECK_INT(t_program(NULL, addr + sizeof(hdr), "world", 5U), 0);
    }

    /* 重新上电：半成品不可见，旧值仍可读；该区被标记为需要压缩 */
    TF_CHECK_INT(kv_open(0U), FKV_OK);
    TF_CHECK_INT(fkv_mount(&s_kv), FKV_OK);
    TF_CHECK_INT(expect_value(7U, "hello", 5U), FKV_OK);
    TF_CHECK_INT(s_kv.write_off, T_ERASE);
    {
        uint32_t before = s_erase_count;

        TF_CHECK_INT(fkv_put(&s_kv, 7U, "world", 5U), FKV_OK);
        TF_CHECK(s_erase_count > before);       /* 触发滚动压缩 */
        TF_CHECK_INT(expect_value(7U, "world", 5U), FKV_OK);
    }
    tf_end();

    tf_begin("fkv/写满轮转");
    kv_reset();
    TF_CHECK_INT(kv_format_open(), FKV_OK);
    {
        uint32_t seq_no;

        for (seq_no = 0U; seq_no < 80U; seq_no++) {
            uint8_t buf[8];

            memset(buf, 0, sizeof(buf));
            buf[0] = (uint8_t)seq_no;
            buf[1] = (uint8_t)(seq_no >> 8);
            TF_CHECK_INT(fkv_put(&s_kv, seq_no % 6U, buf, sizeof(buf)),
                         FKV_OK);
        }
        TF_CHECK(s_kv.generation > 1U);
        TF_CHECK(s_erase_count >= 3U);
        for (i = 0U; i < 6U; i++) {
            uint8_t buf[8];
            uint32_t last = 0U;
            uint32_t j;

            for (j = 0U; j < 80U; j++) {
                if ((j % 6U) == i) {
                    last = j;               /* 每个 key 的最后一次写入 */
                }
            }
            memset(buf, 0, sizeof(buf));
            buf[0] = (uint8_t)last;
            buf[1] = (uint8_t)(last >> 8);
            TF_CHECK_INT(expect_value(i, buf, sizeof(buf)), FKV_OK);
        }
        TF_CHECK_INT(fkv_check(&s_kv), FKV_OK);
    }
    tf_end();

    tf_begin("fkv/手动压缩与遍历");
    kv_reset();
    TF_CHECK_INT(kv_format_open(), FKV_OK);
    TF_CHECK_INT(fkv_put(&s_kv, 10U, "aaa", 3U), FKV_OK);
    TF_CHECK_INT(fkv_put(&s_kv, 20U, "bb", 2U), FKV_OK);
    TF_CHECK_INT(fkv_put(&s_kv, 30U, "c", 1U), FKV_OK);
    TF_CHECK_INT(fkv_del(&s_kv, 20U), FKV_OK);
    {
        uint32_t gen = s_kv.generation;
        uint32_t before = s_erase_count;
        visit_ctx_t v;

        TF_CHECK_INT(fkv_gc(&s_kv), FKV_OK);
        TF_CHECK_INT(s_kv.generation, gen + 1U);
        TF_CHECK(s_erase_count > before);
        TF_CHECK_INT(expect_value(10U, "aaa", 3U), FKV_OK);
        TF_CHECK_INT(expect_value(30U, "c", 1U), FKV_OK);
        TF_CHECK_INT(fkv_exists(&s_kv, 20U), 0);

        /* 遍历：只剩 10 与 30 两个存活键 */
        memset(&v, 0, sizeof(v));
        TF_CHECK_INT(fkv_foreach(&s_kv, visit_cb, &v), FKV_OK);
        TF_CHECK_INT(v.count, 2U);
        TF_CHECK_INT(v.key_sum, 40U);
        TF_CHECK_INT(v.bytes, 4U);
    }
    tf_end();

    tf_begin("fkv/只读挂载");
    TF_CHECK_INT(kv_open(1U), FKV_OK);
    TF_CHECK_INT(fkv_mount(&s_kv), FKV_OK);
    TF_CHECK_INT(fkv_put(&s_kv, 10U, "zzz", 3U), FKV_ERR_READONLY);
    TF_CHECK_INT(fkv_del(&s_kv, 10U), FKV_ERR_READONLY);
    TF_CHECK_INT(fkv_format(&s_kv), FKV_ERR_READONLY);
    TF_CHECK_INT(expect_value(10U, "aaa", 3U), FKV_OK);
    TF_CHECK_INT(fkv_check(&s_kv), FKV_OK);
    tf_end();
}
