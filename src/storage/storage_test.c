/* 存储后端注册表测试：注册/注销、读写擦转发、缺失回调与容量上限。 */
#include "test.h"
#include "storage.h"

#include <string.h>

typedef struct {
    int init_calls;
    uint32_t last_addr;
    uint32_t last_len;
    uint8_t mem[64];
    uint8_t fill;
    int fail;
} mock_store_t;

static mock_store_t s_store;
static mock_store_t s_store2;
static mock_store_t s_store_ro;
static mock_store_t s_store_initfail;

static int mock_init(void *ctx)
{
    mock_store_t *m = (mock_store_t *)ctx;
    m->init_calls++;
    return 0;
}

static int mock_read(void *ctx, uint32_t addr, void *data, uint32_t len)
{
    mock_store_t *m = (mock_store_t *)ctx;
    m->last_addr = addr;
    m->last_len = len;
    if (m->fail) {
        return -1;
    }
    if (data == NULL || addr + len > sizeof(m->mem)) {
        return -1;
    }
    memcpy(data, &m->mem[addr], len);
    return 0;
}

static int mock_write(void *ctx, uint32_t addr, const void *data, uint32_t len)
{
    mock_store_t *m = (mock_store_t *)ctx;
    m->last_addr = addr;
    m->last_len = len;
    if (m->fail) {
        return -1;
    }
    if (data == NULL || addr + len > sizeof(m->mem)) {
        return -1;
    }
    memcpy(&m->mem[addr], data, len);
    return 0;
}

static int mock_erase(void *ctx, uint32_t addr, uint32_t len)
{
    mock_store_t *m = (mock_store_t *)ctx;
    m->last_addr = addr;
    m->last_len = len;
    if (m->fail) {
        return -1;
    }
    if (addr + len > sizeof(m->mem)) {
        return -1;
    }
    memset(&m->mem[addr], 0xFF, len);
    return 0;
}

static int mock_init_fail(void *ctx)
{
    mock_store_t *m = (mock_store_t *)ctx;
    m->init_calls++;
    return -1;
}

static const storage_ops_t s_full_ops = {
    mock_init, mock_read, mock_write, mock_erase, &s_store
};

static const storage_ops_t s_full_ops2 = {
    mock_init, mock_read, mock_write, mock_erase, &s_store2
};

static const storage_ops_t s_ro_ops = {
    NULL, mock_read, NULL, NULL, &s_store_ro
};

static const storage_ops_t s_initfail_ops = {
    mock_init_fail, mock_read, mock_write, mock_erase, &s_store_initfail
};

static void reset_mock(mock_store_t *m)
{
    memset(m, 0, sizeof(*m));
}

void ut_storage(void)
{
    uint8_t w[4];
    uint8_t r[4];
    char names[8][8];
    int i;

    w[0] = 1;
    w[1] = 2;
    w[2] = 3;
    w[3] = 4;

    tf_begin("storage/注册参数校验");
    TF_CHECK_INT(storage_register(NULL, &s_full_ops), -1);
    TF_CHECK_INT(storage_register("flash0", NULL), -1);
    tf_end();

    tf_begin("storage/注册并触发 init 回调");
    reset_mock(&s_store);
    TF_CHECK_INT(storage_register("flash0", &s_full_ops), 0);
    TF_CHECK_INT(s_store.init_calls, 1);
    tf_end();

    tf_begin("storage/读写擦转发");
    memset(r, 0, sizeof(r));
    TF_CHECK_INT(storage_write("flash0", 8, w, sizeof(w)), 0);
    TF_CHECK_INT(s_store.last_addr, 8);
    TF_CHECK_INT(s_store.last_len, 4);

    TF_CHECK_INT(storage_read("flash0", 8, r, sizeof(r)), 0);
    TF_CHECK_INT(s_store.last_addr, 8);
    TF_CHECK_INT(s_store.last_len, 4);
    TF_CHECK_MEM(r, w, 4);

    TF_CHECK_INT(storage_erase("flash0", 8, 4), 0);
    TF_CHECK_INT(s_store.last_addr, 8);
    TF_CHECK_INT(s_store.last_len, 4);
    memset(r, 0, sizeof(r));
    TF_CHECK_INT(storage_read("flash0", 8, r, sizeof(r)), 0);
    TF_CHECK_INT(r[0], 0xFF);
    TF_CHECK_INT(r[3], 0xFF);
    tf_end();

    tf_begin("storage/未知名称与空名称");
    TF_CHECK_INT(storage_read("nope", 0, r, 4), -1);
    TF_CHECK_INT(storage_write("nope", 0, w, 4), -1);
    TF_CHECK_INT(storage_erase("nope", 0, 4), -1);
    TF_CHECK_INT(storage_read(NULL, 0, r, 4), -1);
    TF_CHECK_INT(storage_write(NULL, 0, w, 4), -1);
    TF_CHECK_INT(storage_erase(NULL, 0, 4), -1);
    tf_end();

    tf_begin("storage/底层失败传播");
    s_store.fail = 1;
    TF_CHECK_INT(storage_read("flash0", 0, r, 4), -1);
    TF_CHECK_INT(storage_write("flash0", 0, w, 4), -1);
    TF_CHECK_INT(storage_erase("flash0", 0, 4), -1);
    s_store.fail = 0;
    tf_end();

    tf_begin("storage/注销");
    TF_CHECK_INT(storage_unregister(NULL), -1);
    TF_CHECK_INT(storage_unregister("nope"), -1);
    TF_CHECK_INT(storage_unregister("flash0"), 0);
    TF_CHECK_INT(storage_read("flash0", 0, r, 4), -1);
    TF_CHECK_INT(storage_unregister("flash0"), -1);

    /* 注销后可重新注册并再次初始化 */
    TF_CHECK_INT(storage_register("flash0", &s_full_ops), 0);
    TF_CHECK_INT(s_store.init_calls, 2);
    TF_CHECK_INT(storage_read("flash0", 0, r, 4), 0);
    TF_CHECK_INT(storage_unregister("flash0"), 0);
    tf_end();

    tf_begin("storage/同名重复注册按先注册者查找");
    reset_mock(&s_store);
    reset_mock(&s_store2);
    TF_CHECK_INT(storage_register("dup", &s_full_ops), 0);
    TF_CHECK_INT(storage_register("dup", &s_full_ops2), 0);

    TF_CHECK_INT(storage_write("dup", 1, w, 2), 0);
    TF_CHECK_INT(s_store.last_addr, 1);
    TF_CHECK_INT(s_store2.last_addr, 0);

    /* 注销首个后由第二个实例接管 */
    TF_CHECK_INT(storage_unregister("dup"), 0);
    TF_CHECK_INT(storage_write("dup", 3, w, 2), 0);
    TF_CHECK_INT(s_store.last_addr, 1);
    TF_CHECK_INT(s_store2.last_addr, 3);
    TF_CHECK_INT(storage_unregister("dup"), 0);
    TF_CHECK_INT(storage_read("dup", 0, r, 4), -1);
    tf_end();

    tf_begin("storage/缺少 write/erase 回调");
    reset_mock(&s_store_ro);
    TF_CHECK_INT(storage_register("ro", &s_ro_ops), 0);
    TF_CHECK_INT(storage_write("ro", 0, w, 4), -1);
    TF_CHECK_INT(storage_erase("ro", 0, 4), -1);
    TF_CHECK_INT(storage_read("ro", 0, r, 4), 0);
    TF_CHECK_INT(storage_unregister("ro"), 0);
    tf_end();

    tf_begin("storage/init 回调失败不影响注册");
    reset_mock(&s_store_initfail);
    TF_CHECK_INT(storage_register("initfail", &s_initfail_ops), 0);
    TF_CHECK_INT(s_store_initfail.init_calls, 1);
    TF_CHECK_INT(storage_unregister("initfail"), 0);
    tf_end();

    tf_begin("storage/注册表容量上限");
    for (i = 0; i < 8; i++) {
        snprintf(names[i], sizeof(names[i]), "m%d", i);
        TF_CHECK_INT(storage_register(names[i], &s_full_ops), 0);
    }
    TF_CHECK_INT(storage_register("m8", &s_full_ops), -1);
    for (i = 0; i < 8; i++) {
        TF_CHECK_INT(storage_unregister(names[i]), 0);
    }
    TF_CHECK_INT(storage_read("m0", 0, r, 4), -1);
    tf_end();
}
