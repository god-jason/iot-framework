/* src/iot_ringbuf.c per-file 单元测试。 */
#include "test_framework.h"
#include "iot_ringbuf.h"

#include <string.h>

void ut_iot_ringbuf(void)
{
    iot_ringbuf_t rb;
    uint8_t buffer[8];
    uint8_t out[16];
    static const uint8_t zeros[16];

    tf_begin("ringbuf 初始化参数校验");
    memset(buffer, 0, sizeof(buffer));
    TF_CHECK_INT(iot_ringbuf_init(NULL, buffer, sizeof(buffer)), -1);
    TF_CHECK_INT(iot_ringbuf_init(&rb, NULL, sizeof(buffer)), -1);
    TF_CHECK_INT(iot_ringbuf_init(&rb, buffer, 0), -1);
    TF_CHECK_INT(iot_ringbuf_init(&rb, buffer, sizeof(buffer)), IOT_OK);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 0);
    TF_CHECK_INT(iot_ringbuf_free(&rb), 8);
    TF_CHECK_INT(iot_ringbuf_available(NULL), 0);
    TF_CHECK_INT(iot_ringbuf_free(NULL), 0);
    iot_ringbuf_reset(NULL);
    tf_end();

    tf_begin("ringbuf 空缓冲读写");
    memset(out, 0, sizeof(out));
    TF_CHECK_INT(iot_ringbuf_read(&rb, out, 4), 0);
    TF_CHECK_INT(iot_ringbuf_peek(&rb, out, 4), 0);
    TF_CHECK_INT(iot_ringbuf_skip(&rb, 4), 0);
    TF_CHECK_MEM(out, zeros, 4);
    TF_CHECK_INT(iot_ringbuf_write(&rb, NULL, 4), 0);
    TF_CHECK_INT(iot_ringbuf_read(&rb, NULL, 4), 0);
    TF_CHECK_INT(iot_ringbuf_peek(&rb, NULL, 4), 0);
    TF_CHECK_INT(iot_ringbuf_write(&rb, "x", 0), 0);
    TF_CHECK_INT(iot_ringbuf_read(&rb, out, 0), 0);
    TF_CHECK_INT(iot_ringbuf_peek(&rb, out, 0), 0);
    TF_CHECK_INT(iot_ringbuf_skip(&rb, 0), 0);
    TF_CHECK_INT(iot_ringbuf_write(NULL, "x", 1), 0);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 0);
    tf_end();

    tf_begin("ringbuf 写入容量与溢出");
    TF_CHECK_INT(iot_ringbuf_write(&rb, "hello", 5), 5);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 5);
    TF_CHECK_INT(iot_ringbuf_free(&rb), 3);
    /* 仅剩 3 字节空间，返回实际写入量 */
    TF_CHECK_INT(iot_ringbuf_write(&rb, "XYZ12", 5), 3);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 8);
    TF_CHECK_INT(iot_ringbuf_free(&rb), 0);
    TF_CHECK_INT(iot_ringbuf_write(&rb, "!", 1), 0);
    tf_end();

    tf_begin("ringbuf peek 不消费 / 读取不足");
    memset(out, 0, sizeof(out));
    TF_CHECK_INT(iot_ringbuf_peek(&rb, out, 8), 8);
    TF_CHECK_MEM(out, "helloXYZ", 8);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 8);

    TF_CHECK_INT(iot_ringbuf_read(&rb, out, 16), 8);   /* 请求超过可用 */
    TF_CHECK_MEM(out, "helloXYZ", 8);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 0);
    TF_CHECK_INT(iot_ringbuf_read(&rb, out, 1), 0);
    tf_end();

    tf_begin("ringbuf 环绕写入");
    TF_CHECK_INT(iot_ringbuf_write(&rb, "abcdef", 6), 6);
    TF_CHECK_INT(iot_ringbuf_read(&rb, out, 6), 6);
    TF_CHECK_MEM(out, "abcdef", 6);
    /* write_pos 已越过容量边界，再写入应回绕到索引 0 */
    TF_CHECK_INT(iot_ringbuf_write(&rb, "12345678", 8), 8);
    TF_CHECK_INT(iot_ringbuf_read(&rb, out, 8), 8);
    TF_CHECK_MEM(out, "12345678", 8);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 0);
    tf_end();

    tf_begin("ringbuf skip 与 reset");
    iot_ringbuf_write(&rb, "01234567", 8);
    TF_CHECK_INT(iot_ringbuf_skip(&rb, 3), 3);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 5);
    TF_CHECK_INT(iot_ringbuf_read(&rb, out, 5), 5);
    TF_CHECK_MEM(out, "34567", 5);

    iot_ringbuf_write(&rb, "abc", 3);
    TF_CHECK_INT(iot_ringbuf_skip(&rb, 10), 3);   /* 超过可用则截断 */
    TF_CHECK_INT(iot_ringbuf_available(&rb), 0);

    iot_ringbuf_write(&rb, "xyz", 3);
    iot_ringbuf_reset(&rb);
    TF_CHECK_INT(iot_ringbuf_available(&rb), 0);
    TF_CHECK_INT(iot_ringbuf_free(&rb), 8);
    TF_CHECK_INT(iot_ringbuf_read(&rb, out, 1), 0);
    tf_end();

    tf_begin("ringbuf 容量 1 边界");
    {
        uint8_t small[1];
        iot_ringbuf_t rb1;
        TF_CHECK_INT(iot_ringbuf_init(&rb1, small, 1), IOT_OK);
        TF_CHECK_INT(iot_ringbuf_write(&rb1, "A", 1), 1);
        TF_CHECK_INT(iot_ringbuf_write(&rb1, "B", 1), 0);
        TF_CHECK_INT(iot_ringbuf_peek(&rb1, out, 1), 1);
        TF_CHECK_INT(out[0], 'A');
        TF_CHECK_INT(iot_ringbuf_read(&rb1, out, 1), 1);
        TF_CHECK_INT(out[0], 'A');
        TF_CHECK_INT(iot_ringbuf_write(&rb1, "B", 1), 1);
        TF_CHECK_INT(iot_ringbuf_read(&rb1, out, 1), 1);
        TF_CHECK_INT(out[0], 'B');
    }
    tf_end();
}
