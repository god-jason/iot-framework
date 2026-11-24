/*
 * config_test.c - 双区轮转配置存储测试
 *
 * 用两段 RAM 模拟 Flash（编程只能 1→0、擦除按扇区），覆盖：
 * 空设备、保存/读取、未提交记录（掉电）、写满轮转、非法头部、格式化。
 */

#include "test.h"
#include "config.h"

#define SECTOR_SIZE     256U
#define SECTOR_A        0x1000U
#define SECTOR_B        0x2000U
#define MAX_DATA        96U

typedef struct {
    uint32_t value;
    uint16_t tag;
} sample_t;

static uint8_t s_flash[SECTOR_SIZE * 2U];
static uint32_t s_erase_count;
static config_store_t s_store;

static uint32_t flash_index(uint32_t addr)
{
    if (addr >= SECTOR_B) {
        return SECTOR_SIZE + (addr - SECTOR_B);
    }
    return addr - SECTOR_A;
}

static int ram_read(uint32_t addr, void *buf, uint32_t size)
{
    uint32_t i = flash_index(addr);

    if (i > sizeof(s_flash) || size > sizeof(s_flash) - i) {
        return -1;
    }
    memcpy(buf, &s_flash[i], size);
    return 0;
}

static int ram_write(uint32_t addr, const void *buf, uint32_t size)
{
    uint32_t i = flash_index(addr);
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t k;

    if (i > sizeof(s_flash) || size > sizeof(s_flash) - i) {
        return -1;
    }
    for (k = 0U; k < size; k++) {
        s_flash[i + k] &= src[k];       /* Flash：编程只能把 1 变成 0 */
    }
    return 0;
}

static int ram_erase(uint32_t addr, uint32_t size)
{
    if ((addr != SECTOR_A && addr != SECTOR_B) || size != SECTOR_SIZE) {
        return -1;
    }
    memset(&s_flash[flash_index(addr)], 0xFF, size);
    s_erase_count++;
    return 0;
}

static const config_store_ops_t s_ops = {
    ram_read, ram_write, ram_erase
};

static const config_store_layout_t s_layout = {
    { SECTOR_A, SECTOR_B }, SECTOR_SIZE, 4U, MAX_DATA, 1U
};

static void store_reset(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_erase_count = 0U;
    memset(&s_store, 0, sizeof(s_store));
}

static int save_value(uint32_t value)
{
    sample_t s;

    memset(&s, 0, sizeof(s));           /* padding 也要确定，才能参与 CRC */
    s.value = value;
    s.tag = (uint16_t)value;
    return config_store_save(&s_store, &s, sizeof(s));
}

static int load_value(uint32_t *value)
{
    sample_t s;
    int rc;

    memset(&s, 0, sizeof(s));
    rc = config_store_load(&s_store, &s, sizeof(s));
    if (rc == CONFIG_STORE_OK) {
        *value = s.value;
    }
    return rc;
}

void ut_config(void)
{
    uint32_t value = 0U;
    uint32_t len = 0U;
    uint16_t version = 0U;
    int rc;

    tf_begin("config/空设备");
    store_reset();
    TF_CHECK_INT(config_store_init(&s_store, &s_layout, &s_ops),
                 CONFIG_STORE_NONE);
    TF_CHECK_INT(load_value(&value), CONFIG_STORE_NONE);
    TF_CHECK(s_store.has_record == 0U);
    tf_end();

    tf_begin("config/保存与读取");
    store_reset();
    TF_CHECK_INT(config_store_init(&s_store, &s_layout, &s_ops),
                 CONFIG_STORE_NONE);
    TF_CHECK_INT(save_value(11U), CONFIG_STORE_OK);
    TF_CHECK_INT(load_value(&value), CONFIG_STORE_OK);
    TF_CHECK_INT(value, 11U);
    TF_CHECK_INT(save_value(22U), CONFIG_STORE_OK);
    TF_CHECK_INT(load_value(&value), CONFIG_STORE_OK);
    TF_CHECK_INT(value, 22U);
    TF_CHECK_INT(s_store.sequence, 2U);
    TF_CHECK_INT(s_erase_count, 0U);            /* 追加不擦除 */

    /* 长度不一致 / 超限 */
    TF_CHECK_INT(config_store_load(&s_store, &value, 4U),
                 CONFIG_STORE_ERR_SIZE);
    {
        uint8_t big[MAX_DATA + 4U];
        memset(big, 0, sizeof(big));
        TF_CHECK_INT(config_store_save(&s_store, big, sizeof(big)),
                     CONFIG_STORE_ERR_SIZE);
    }

    /* 原始读取：输出长度与版本 */
    {
        sample_t raw;
        TF_CHECK_INT(config_store_load_raw(&s_store, &raw, sizeof(raw),
                                           &len, &version), CONFIG_STORE_OK);
        TF_CHECK_INT(len, sizeof(sample_t));
        TF_CHECK_INT(version, 1U);
        TF_CHECK_INT(raw.value, 22U);
    }
    tf_end();

    tf_begin("config/未提交记录（掉电）");
    store_reset();
    TF_CHECK_INT(config_store_init(&s_store, &s_layout, &s_ops),
                 CONFIG_STORE_NONE);
    TF_CHECK_INT(save_value(33U), CONFIG_STORE_OK);

    /* 在追加位置伪造一条“头部已写、Commit 未写”的半成品记录 */
    {
        config_store_hdr_t h;
        memset(&h, 0xFF, sizeof(h));
        h.magic = CONFIG_STORE_MAGIC;
        h.version = 1U;
        h.length = (uint16_t)sizeof(sample_t);
        h.sequence = 99U;
        h.crc32 = 0xDEADBEEFU;
        TF_CHECK_INT(ram_write(SECTOR_A + 32U, &h, sizeof(h)), 0);
    }

    /* 重新上电：半成品被跳过，仍读到上一条完整记录 */
    memset(&s_store, 0, sizeof(s_store));
    TF_CHECK_INT(config_store_init(&s_store, &s_layout, &s_ops),
                 CONFIG_STORE_OK);
    TF_CHECK_INT(load_value(&value), CONFIG_STORE_OK);
    TF_CHECK_INT(value, 33U);
    TF_CHECK_INT(s_store.sequence, 1U);
    TF_CHECK_INT(s_store.write_offset, 64U);

    /* 续写落在半成品之后，新旧配置都可读 */
    TF_CHECK_INT(save_value(44U), CONFIG_STORE_OK);
    TF_CHECK_INT(load_value(&value), CONFIG_STORE_OK);
    TF_CHECK_INT(value, 44U);
    tf_end();

    tf_begin("config/写满轮转");
    store_reset();
    TF_CHECK_INT(config_store_init(&s_store, &s_layout, &s_ops),
                 CONFIG_STORE_NONE);
    /* 8 字节数据 → 每条 32 字节，每区 8 条；第 9 条触发轮转 */
    for (value = 1U; value <= 9U; value++) {
        TF_CHECK_INT(save_value(value), CONFIG_STORE_OK);
    }
    TF_CHECK_INT(s_erase_count, 1U);
    TF_CHECK_INT(s_store.active_index, 1U);
    TF_CHECK_INT(load_value(&value), CONFIG_STORE_OK);
    TF_CHECK_INT(value, 9U);

    /* 再写满第二区，轮转回第一区 */
    for (rc = 10; rc <= 17; rc++) {
        TF_CHECK_INT(save_value((uint32_t)rc), CONFIG_STORE_OK);
    }
    TF_CHECK_INT(s_erase_count, 2U);
    TF_CHECK_INT(s_store.active_index, 0U);
    TF_CHECK_INT(load_value(&value), CONFIG_STORE_OK);
    TF_CHECK_INT(value, 17U);
    TF_CHECK_INT(s_store.sequence, 17U);
    tf_end();

    tf_begin("config/非法头部");
    store_reset();
    TF_CHECK_INT(config_store_init(&s_store, &s_layout, &s_ops),
                 CONFIG_STORE_NONE);
    {
        uint32_t garbage = 0x12345678U;
        TF_CHECK_INT(ram_write(SECTOR_A, &garbage, sizeof(garbage)), 0);
    }
    memset(&s_store, 0, sizeof(s_store));
    TF_CHECK_INT(config_store_init(&s_store, &s_layout, &s_ops),
                 CONFIG_STORE_NONE);
    TF_CHECK_INT(s_store.active_index, 1U);            /* A 区不可用，改从 B 追加 */
    TF_CHECK_INT(s_store.write_offset, 0U);
    TF_CHECK_INT(save_value(55U), CONFIG_STORE_OK);
    TF_CHECK_INT(s_store.active_index, 1U);            /* 写入另一区 */
    TF_CHECK_INT(load_value(&value), CONFIG_STORE_OK);
    TF_CHECK_INT(value, 55U);
    tf_end();

    tf_begin("config/格式化");
    store_reset();
    TF_CHECK_INT(config_store_init(&s_store, &s_layout, &s_ops),
                 CONFIG_STORE_NONE);
    TF_CHECK_INT(save_value(66U), CONFIG_STORE_OK);
    TF_CHECK_INT(config_store_format(&s_store), CONFIG_STORE_OK);
    TF_CHECK_INT(load_value(&value), CONFIG_STORE_NONE);
    TF_CHECK_INT(save_value(77U), CONFIG_STORE_OK);
    TF_CHECK_INT(load_value(&value), CONFIG_STORE_OK);
    TF_CHECK_INT(value, 77U);
    tf_end();
}
