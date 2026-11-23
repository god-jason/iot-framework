/*
 * config_test.c - 配置抽象层测试
 *
 * 用 RAM 模拟板级 port_config_xxx（强符号覆盖 config.c 的 weak 默认值），
 * 覆盖有效性、读写、容量限制与擦除。
 */

#include "test.h"
#include "config.h"

#define STORE_CAP   32U

static uint8_t s_store[STORE_CAP];
static uint32_t s_store_len;
static bool s_valid;

static void mock_reset(void)
{
    memset(s_store, 0, sizeof(s_store));
    s_store_len = 0U;
    s_valid = false;
}

/* ---- 板级后端模拟（强符号） ---- */

int port_config_read(void *buf, uint32_t max, uint32_t *len)
{
    if (!s_valid || buf == NULL || s_store_len > max) {
        return CONFIG_ERR;
    }
    memcpy(buf, s_store, s_store_len);
    if (len != NULL) {
        *len = s_store_len;
    }
    return CONFIG_OK;
}

int port_config_write(const void *data, uint32_t len)
{
    if (data == NULL || len > STORE_CAP) {
        return CONFIG_ERR;
    }
    memcpy(s_store, data, len);
    s_store_len = len;
    s_valid = true;
    return CONFIG_OK;
}

int port_config_erase(void)
{
    memset(s_store, 0, sizeof(s_store));
    s_store_len = 0U;
    s_valid = false;
    return CONFIG_OK;
}

bool port_config_valid(void)
{
    return s_valid;
}

void ut_config(void)
{
    uint8_t data[8] = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U };
    uint8_t buf[STORE_CAP];
    uint32_t len = 0U;

    tf_begin("config/初始状态与参数");

    mock_reset();
    TF_CHECK(!config_is_valid());
    TF_CHECK_INT(config_read(buf, sizeof(buf), &len), CONFIG_ERR);
    TF_CHECK_INT(config_read(NULL, sizeof(buf), &len), CONFIG_ERR);
    TF_CHECK_INT(config_write(NULL, 1U), CONFIG_ERR);

    tf_end();

    tf_begin("config/写入与读取");

    mock_reset();
    TF_CHECK_INT(config_write(data, sizeof(data)), CONFIG_OK);
    TF_CHECK(config_is_valid());

    TF_CHECK_INT(config_read(buf, sizeof(buf), &len), CONFIG_OK);
    TF_CHECK_INT(len, sizeof(data));
    TF_CHECK_INT(memcmp(buf, data, sizeof(data)), 0);

    /* 容量不足 */
    TF_CHECK_INT(config_read(buf, 4U, &len), CONFIG_ERR);

    /* len 输出可为 NULL */
    TF_CHECK_INT(config_read(buf, sizeof(buf), NULL), CONFIG_OK);

    tf_end();

    tf_begin("config/擦除");

    mock_reset();
    TF_CHECK_INT(config_write(data, sizeof(data)), CONFIG_OK);
    TF_CHECK_INT(config_erase(), CONFIG_OK);
    TF_CHECK(!config_is_valid());
    TF_CHECK_INT(config_read(buf, sizeof(buf), &len), CONFIG_ERR);

    tf_end();
}
