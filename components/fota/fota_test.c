/*
 * fota_test.c - FOTA 会话层测试
 *
 * 板级后端用 RAM 模拟（强符号覆盖 fota.c 中的 weak 默认实现）：
 * 覆盖连续写入约束、完成/放弃会话、fota_run 下载流程与状态读取。
 */

#include "test.h"
#include "fota.h"

#define OTA_CAP     64U
#define IMAGE_LEN   16U

static uint8_t s_flash[OTA_CAP];
static uint32_t s_image_len;
static uint32_t s_image_crc;
static bool s_finished;
static int s_abort_calls;
static fota_state_t s_state;

static void mock_reset(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_image_len = 0U;
    s_image_crc = 0U;
    s_finished = false;
    s_abort_calls = 0;
    memset(&s_state, 0, sizeof(s_state));
}

/* ---- 板级后端模拟（强符号覆盖 weak 默认实现） ---- */

int board_ota_begin(uint32_t image_size, uint32_t image_crc)
{
    if (image_size == 0U || image_size > OTA_CAP) {
        return FOTA_ERR;
    }
    memset(s_flash, 0xFF, sizeof(s_flash));
    s_image_len = image_size;
    s_image_crc = image_crc;
    s_finished = false;
    return FOTA_OK;
}

int board_ota_write(uint32_t offset, const void *data, uint32_t len)
{
    if ((offset + len) > OTA_CAP) {
        return FOTA_ERR;
    }
    memcpy(&s_flash[offset], data, len);
    return FOTA_OK;
}

int board_ota_finish(void)
{
    s_finished = true;
    s_state.ota_valid = true;
    s_state.ota_len = s_image_len;
    s_state.ota_crc = s_image_crc;
    return FOTA_OK;
}

void board_ota_abort(void)
{
    s_abort_calls++;
}

int board_ota_state(fota_state_t *state)
{
    *state = s_state;
    return FOTA_OK;
}

/* ---- fota_run 数据源 ---- */

static uint8_t s_src[IMAGE_LEN];
static uint32_t s_src_off;
static bool s_src_short;

static int read_src(uint8_t *buf, uint32_t max, void *user)
{
    uint32_t left;

    (void)user;
    if (s_src_short) {
        return 0;
    }
    left = IMAGE_LEN - s_src_off;
    if (left == 0U) {
        return 0;
    }
    if (left > max) {
        left = max;
    }
    memcpy(buf, &s_src[s_src_off], left);
    s_src_off += left;
    return (int)left;
}

void ut_fota(void)
{
    fota_state_t st;
    uint8_t data[IMAGE_LEN];
    uint32_t i;

    for (i = 0U; i < IMAGE_LEN; i++) {
        s_src[i] = (uint8_t)(0x30U + i);
        data[i] = (uint8_t)(0xA0U + i);
    }

    tf_begin("fota/参数与会话约束");

    mock_reset();
    fota_abort();
    TF_CHECK_INT(fota_begin(0U, 0U), FOTA_ERR);
    TF_CHECK_INT(fota_begin(IMAGE_LEN, 0x1234U), FOTA_OK);
    TF_CHECK_INT(fota_begin(IMAGE_LEN, 0x1234U), FOTA_ERR);        /* 已有会话 */
    TF_CHECK_INT(fota_write(1U, data, 4U), FOTA_ERR);              /* 偏移不连续 */
    TF_CHECK_INT(fota_write(0U, data, IMAGE_LEN + 1U), FOTA_ERR);  /* 越界 */
    TF_CHECK_INT(fota_write(0U, data, IMAGE_LEN), FOTA_OK);
    TF_CHECK_INT(fota_write(IMAGE_LEN, data, 1U), FOTA_ERR);       /* 已写满 */
    TF_CHECK_INT(fota_finish(), FOTA_OK);
    TF_CHECK(s_finished);

    TF_CHECK_INT(fota_get_state(&st), FOTA_OK);
    TF_CHECK(st.ota_valid);
    TF_CHECK_INT(st.ota_len, IMAGE_LEN);
    TF_CHECK_INT(st.ota_crc, 0x1234U);

    tf_end();

    tf_begin("fota/未写完与放弃");

    mock_reset();
    TF_CHECK_INT(fota_begin(IMAGE_LEN, 0x55U), FOTA_OK);
    TF_CHECK_INT(fota_write(0U, data, 8U), FOTA_OK);
    TF_CHECK_INT(fota_finish(), FOTA_ERR);                        /* 未写满 */
    fota_abort();
    TF_CHECK_INT(s_abort_calls, 1);
    TF_CHECK_INT(fota_finish(), FOTA_ERR);                        /* 会话已结束 */
    TF_CHECK(!s_finished);

    tf_end();

    tf_begin("fota/fota_run 下载");

    mock_reset();
    s_src_off = 0U;
    s_src_short = false;
    TF_CHECK_INT(fota_run(IMAGE_LEN, 0x66U, read_src, NULL), FOTA_OK);
    TF_CHECK(s_finished);
    TF_CHECK_INT(memcmp(s_flash, s_src, IMAGE_LEN), 0);

    mock_reset();
    s_src_off = 0U;
    s_src_short = true;
    TF_CHECK_INT(fota_run(IMAGE_LEN, 0x66U, read_src, NULL), FOTA_ERR);
    TF_CHECK_INT(s_abort_calls, 1);
    TF_CHECK(!s_finished);

    tf_end();

    tf_begin("fota/状态接口");

    TF_CHECK_INT(fota_get_state(NULL), FOTA_ERR);

    tf_end();
}
