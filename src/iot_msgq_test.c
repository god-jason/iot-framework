/*
 * iot_msgq_test.c - 变长消息队列测试
 *
 * 覆盖：初始化校验、变长消息收发、环形回绕、空间不足、
 *       接收缓冲不足（消息保留）、长度查询、FromISR、动态创建/销毁。
 */

#include "test.h"
#include "iot.h"

#include <string.h>

void ut_msgq(void)
{
    uint8_t buf[32];
    uint8_t out[32];
    uint32_t out_len = 0U;
    iot_msgq_t mq;
    bool need_yield = false;
    int i;

    tf_begin("iot/msgq 变长消息队列");

    /* -------- 初始化校验 -------- */
    TF_CHECK_INT(iot_msgq_init(NULL, buf, sizeof(buf)), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_msgq_init(&mq, NULL, sizeof(buf)), IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_msgq_init(&mq, buf, IOT_MSGQ_HDR_SIZE),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_msgq_init(&mq, buf, sizeof(buf)), IOT_OK);
    TF_CHECK_INT(iot_msgq_count(&mq), 0);
    TF_CHECK_INT(iot_msgq_bytes(&mq), 0);

    /* -------- 单条消息往返 -------- */
    TF_CHECK_INT(iot_msgq_send(&mq, "hello", 5U, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(iot_msgq_count(&mq), 1);
    TF_CHECK_INT(iot_msgq_bytes(&mq), 5 + IOT_MSGQ_HDR_SIZE);

    memset(out, 0, sizeof(out));
    out_len = 0U;
    TF_CHECK_INT(iot_msgq_recv(&mq, out, sizeof(out), &out_len,
                               IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(out_len, 5);
    TF_CHECK(memcmp(out, "hello", 5U) == 0);
    TF_CHECK_INT(iot_msgq_count(&mq), 0);
    TF_CHECK_INT(iot_msgq_bytes(&mq), 0);

    /* 空队列非阻塞接收 */
    TF_CHECK_INT(iot_msgq_recv(&mq, out, sizeof(out), &out_len,
                               IOT_TIMEOUT_NONE), IOT_TIMEOUT);

    /* 超过单条最大长度（size - HDR） */
    TF_CHECK_INT(iot_msgq_send(&mq, out, sizeof(buf), IOT_TIMEOUT_NONE),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_msgq_send(&mq, NULL, 1U, IOT_TIMEOUT_NONE),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_msgq_send(&mq, out, 0U, IOT_TIMEOUT_NONE),
                 IOT_INVALID_PARAM);
    TF_CHECK_INT(iot_msgq_recv(&mq, NULL, 4U, &out_len, IOT_TIMEOUT_NONE),
                 IOT_INVALID_PARAM);

    /* -------- 多条消息 + 接收缓冲不足 -------- */
    TF_CHECK_INT(iot_msgq_send(&mq, "A", 1U, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(iot_msgq_send(&mq, "BB", 2U, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(iot_msgq_send(&mq, "CCC", 3U, IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(iot_msgq_count(&mq), 3);

    out_len = 0U;
    TF_CHECK_INT(iot_msgq_recv(&mq, out, sizeof(out), &out_len,
                               IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(out_len, 1);
    TF_CHECK(out[0] == 'A');

    /* 队首为 "BB"（2 字节），接收缓冲只有 1 字节：返回 OVERFLOW 且不消费 */
    out_len = 0U;
    TF_CHECK_INT(iot_msgq_recv(&mq, out, 1U, &out_len, IOT_TIMEOUT_NONE),
                 IOT_OVERFLOW);
    TF_CHECK_INT(out_len, 2);
    TF_CHECK_INT(iot_msgq_count(&mq), 2);

    /* 仅查询队首长度（msg 为 NULL、max_len 为 0） */
    out_len = 0U;
    TF_CHECK_INT(iot_msgq_recv(&mq, NULL, 0U, &out_len, IOT_TIMEOUT_NONE),
                 IOT_OVERFLOW);
    TF_CHECK_INT(out_len, 2);
    TF_CHECK_INT(iot_msgq_count(&mq), 2);

    TF_CHECK_INT(iot_msgq_recv(&mq, out, sizeof(out), &out_len,
                               IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(out_len, 2);
    TF_CHECK(memcmp(out, "BB", 2U) == 0);

    TF_CHECK_INT(iot_msgq_recv(&mq, out, sizeof(out), &out_len,
                               IOT_TIMEOUT_NONE), IOT_OK);
    TF_CHECK_INT(out_len, 3);
    TF_CHECK(memcmp(out, "CCC", 3U) == 0);
    TF_CHECK_INT(iot_msgq_count(&mq), 0);

    /* -------- 空间不足 -------- */
    {
        uint8_t ten[10] = {0};

        TF_CHECK_INT(iot_msgq_send(&mq, ten, 10U, IOT_TIMEOUT_NONE), IOT_OK);
        TF_CHECK_INT(iot_msgq_send(&mq, ten, 10U, IOT_TIMEOUT_NONE), IOT_OK);
        TF_CHECK_INT(iot_msgq_bytes(&mq), 28);
        /* 剩余 4 字节，放不下第三条 10 字节消息 */
        TF_CHECK_INT(iot_msgq_send(&mq, ten, 10U, IOT_TIMEOUT_NONE),
                     IOT_TIMEOUT);
        TF_CHECK_INT(iot_msgq_count(&mq), 2);

        TF_CHECK_INT(iot_msgq_recv(&mq, out, sizeof(out), &out_len,
                                   IOT_TIMEOUT_NONE), IOT_OK);
        TF_CHECK_INT(out_len, 10);
        TF_CHECK_INT(iot_msgq_send(&mq, ten, 10U, IOT_TIMEOUT_NONE), IOT_OK);
        TF_CHECK_INT(iot_msgq_count(&mq), 2);
    }

    /* -------- 环形回绕：连续收发使读写位置跨越缓冲末尾 -------- */
    TF_CHECK_INT(iot_msgq_reset(&mq), IOT_OK);
    TF_CHECK_INT(iot_msgq_count(&mq), 0);
    TF_CHECK_INT(iot_msgq_bytes(&mq), 0);

    for (i = 0; i < 6; i++) {
        uint8_t msg[6] = {(uint8_t)i, 1U, 2U, 3U, 4U, 5U};

        TF_CHECK_INT(iot_msgq_send(&mq, msg, sizeof(msg), IOT_TIMEOUT_NONE),
                     IOT_OK);
        memset(out, 0, sizeof(out));
        out_len = 0U;
        TF_CHECK_INT(iot_msgq_recv(&mq, out, sizeof(out), &out_len,
                                   IOT_TIMEOUT_NONE), IOT_OK);
        TF_CHECK_INT(out_len, 6);
        TF_CHECK(out[0] == (uint8_t)i);
        TF_CHECK(out[5] == 5U);
    }

    /* -------- FromISR 非阻塞版本 -------- */
    TF_CHECK_INT(iot_msgq_reset(&mq), IOT_OK);
    TF_CHECK_INT(iot_msgq_send_from_isr(&mq, "xy", 2U, &need_yield), IOT_OK);
    out_len = 0U;
    TF_CHECK_INT(iot_msgq_recv_from_isr(&mq, out, sizeof(out), &out_len,
                                        &need_yield), IOT_OK);
    TF_CHECK_INT(out_len, 2);
    TF_CHECK(memcmp(out, "xy", 2U) == 0);
    TF_CHECK_INT(iot_msgq_recv_from_isr(&mq, out, sizeof(out), &out_len,
                                        &need_yield), IOT_TIMEOUT);

    /* -------- 动态创建/销毁 -------- */
    {
        iot_msgq_t *dyn = iot_msgq_create(64U);

        TF_CHECK(dyn != NULL);
        if (dyn != NULL) {
            TF_CHECK_INT(iot_msgq_send(dyn, "dyn", 3U, IOT_TIMEOUT_NONE),
                         IOT_OK);
            out_len = 0U;
            TF_CHECK_INT(iot_msgq_recv(dyn, out, sizeof(out), &out_len,
                                       IOT_TIMEOUT_NONE), IOT_OK);
            TF_CHECK_INT(out_len, 3);
            TF_CHECK(memcmp(out, "dyn", 3U) == 0);
            TF_CHECK_INT(iot_msgq_destroy(dyn), IOT_OK);
        }
        TF_CHECK(iot_msgq_create(IOT_MSGQ_HDR_SIZE) == NULL);
        TF_CHECK_INT(iot_msgq_destroy(NULL), IOT_INVALID_PARAM);
    }

    tf_end();
}
