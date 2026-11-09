/*
 * iot_sbuf_test.c - 流式字节缓冲测试
 *
 * 覆盖：初始化校验、部分读写、环形回绕、重置、动态创建/销毁、
 *       FromISR 非阻塞读写与参数防御。
 */

#include "test.h"
#include "iot.h"

#include <string.h>

void ut_sbuf(void)
{
    uint8_t buf[8];
    uint8_t out[16];
    iot_sbuf_t sb;
    uint8_t seq[8];
    uint8_t wrap_in[5] = {10U, 11U, 12U, 13U, 14U};
    uint8_t wrap_out[8];
    bool need_yield = false;
    int i;

    tf_begin("iot/sbuf 流式缓冲");

    /* -------- 初始化参数校验 -------- */
    TF_CHECK_INT(iot_sbuf_init(NULL, buf, sizeof(buf)), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_sbuf_init(&sb, NULL, sizeof(buf)), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_sbuf_init(&sb, buf, 0U), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_sbuf_init(&sb, buf, sizeof(buf)), IOT_OK);
    TF_CHECK_INT(iot_sbuf_count(&sb), 0);
    TF_CHECK_INT(iot_sbuf_space(&sb), 8);

    /* -------- 部分写入与部分读取 -------- */
    for (i = 0; i < 8; i++) {
        seq[i] = (uint8_t)i;
    }
    TF_CHECK_INT(iot_sbuf_write(&sb, seq, 5U, IOT_TIMEOUT_NONE), 5);
    TF_CHECK_INT(iot_sbuf_count(&sb), 5);
    TF_CHECK_INT(iot_sbuf_space(&sb), 3);

    /* 剩余空间只有 3：非阻塞写入只写 3 字节 */
    TF_CHECK_INT(iot_sbuf_write(&sb, seq + 5, 5U, IOT_TIMEOUT_NONE), 3);
    TF_CHECK_INT(iot_sbuf_count(&sb), 8);
    TF_CHECK_INT(iot_sbuf_space(&sb), 0);

    memset(out, 0, sizeof(out));
    TF_CHECK_INT(iot_sbuf_read(&sb, out, 3U, IOT_TIMEOUT_NONE), 3);
    TF_CHECK_INT(out[0], 0);
    TF_CHECK_INT(out[1], 1);
    TF_CHECK_INT(out[2], 2);
    TF_CHECK_INT(iot_sbuf_count(&sb), 5);

    TF_CHECK_INT(iot_sbuf_read(&sb, out, 5U, IOT_TIMEOUT_NONE), 5);
    TF_CHECK_INT(out[0], 3);
    TF_CHECK_INT(out[4], 7);
    TF_CHECK_INT(iot_sbuf_count(&sb), 0);

    /* 空缓冲非阻塞读返回 0 */
    TF_CHECK_INT(iot_sbuf_read(&sb, out, 4U, IOT_TIMEOUT_NONE), 0);

    /* -------- 环形回绕 -------- */
    TF_CHECK_INT(iot_sbuf_reset(&sb), IOT_OK);
    TF_CHECK_INT(iot_sbuf_write(&sb, seq, 6U, IOT_TIMEOUT_NONE), 6);
    TF_CHECK_INT(iot_sbuf_read(&sb, out, 4U, IOT_TIMEOUT_NONE), 4);
    /* 读走 0..3，剩 4,5；再写 5 字节会跨过缓冲末尾回绕 */
    TF_CHECK_INT(iot_sbuf_write(&sb, wrap_in, 5U, IOT_TIMEOUT_NONE), 5);
    TF_CHECK_INT(iot_sbuf_count(&sb), 7);

    memset(wrap_out, 0, sizeof(wrap_out));
    TF_CHECK_INT(iot_sbuf_read(&sb, wrap_out, 7U, IOT_TIMEOUT_NONE), 7);
    TF_CHECK_INT(wrap_out[0], 4);
    TF_CHECK_INT(wrap_out[1], 5);
    TF_CHECK_INT(wrap_out[2], 10);
    TF_CHECK_INT(wrap_out[6], 14);

    /* -------- 重置 -------- */
    TF_CHECK_INT(iot_sbuf_write(&sb, seq, 3U, IOT_TIMEOUT_NONE), 3);
    TF_CHECK_INT(iot_sbuf_reset(&sb), IOT_OK);
    TF_CHECK_INT(iot_sbuf_count(&sb), 0);
    TF_CHECK_INT(iot_sbuf_space(&sb), 8);

    /* -------- FromISR 非阻塞版本 -------- */
    TF_CHECK_INT(iot_sbuf_write_from_isr(&sb, seq, 4U, &need_yield), 4);
    memset(out, 0, sizeof(out));
    TF_CHECK_INT(iot_sbuf_read_from_isr(&sb, out, 8U, &need_yield), 4);
    TF_CHECK_INT(out[0], 0);
    TF_CHECK_INT(out[3], 3);

    /* -------- 动态创建/销毁 -------- */
    {
        iot_sbuf_t *dyn = iot_sbuf_create(16U);

        TF_CHECK(dyn != NULL);
        if (dyn != NULL) {
            TF_CHECK_INT(iot_sbuf_write(dyn, seq, 4U, IOT_TIMEOUT_NONE), 4);
            TF_CHECK_INT(iot_sbuf_count(dyn), 4);
            TF_CHECK_INT(iot_sbuf_destroy(dyn), IOT_OK);
        }
        TF_CHECK(iot_sbuf_create(0U) == NULL);
        TF_CHECK_INT(iot_sbuf_destroy(NULL), IOT_INVALID_PARAM);
    }

    /* -------- 参数防御 -------- */
    TF_CHECK_INT(iot_sbuf_write(&sb, NULL, 1U, IOT_TIMEOUT_NONE),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_sbuf_write(&sb, seq, 0U, IOT_TIMEOUT_NONE),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_sbuf_read(&sb, NULL, 1U, IOT_TIMEOUT_NONE),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_sbuf_read(&sb, out, 0U, IOT_TIMEOUT_NONE),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_sbuf_count(NULL), 0);
    TF_CHECK_INT(iot_sbuf_space(NULL), 0);

    tf_end();
}
