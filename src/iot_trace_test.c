/*
 * iot_trace_test.c - 事件跟踪回归测试
 *
 * 覆盖：运行期动态开关、宏等级过滤、环形快照、CTF 包导出、重入保护。
 */

#include "test.h"
#include "iot.h"

#include <stdio.h>
#include <string.h>

#define TEST_CAP       2048U
/* CTF magic 0xC1FC1FC1，配置 native-byte-order: le -> C1 1F FC C1 */
#define CTF_MAGIC_0    0xC1U
#define CTF_MAGIC_1    0x1FU
#define CTF_MAGIC_2    0xFCU
#define CTF_MAGIC_3    0xC1U

static uint8_t  s_sink_buf[TEST_CAP];
static uint32_t s_sink_len;
static uint32_t s_sink_calls;

static void test_sink(const uint8_t *data, uint32_t len, void *user)
{
    (void)user;
    if (len <= sizeof(s_sink_buf)) {
        memcpy(s_sink_buf, data, len);
        s_sink_len = len;
    }
    s_sink_calls++;
}

static uint32_t test_clock(void)
{
    return 0x12345678U;
}

/* 返回刚写入的最后一条事件 */
static const iot_trace_event_t *last_event(void)
{
    uint32_t n = iot_trace_count();

    if (n == 0U) {
        return NULL;
    }
    return iot_trace_at((n - 1U) & (iot_trace_capacity() - 1U));
}

void ut_trace(void)
{
#if IOT_USE_TRACE
    const iot_trace_event_t *ev;
    uint32_t i;

    tf_begin("iot/trace 宏与动态开关");

    iot_trace_init();
    iot_trace_reset();
    iot_trace_disable();

    /* 1. 关闭时宏不产生任何事件 */
    TF_CHECK_INT(iot_trace_get_level(), IOT_TRACE_LEVEL_OFF);
    IOT_TRACE(IOT_EVT_MARKER, 1U, 2U);
    IOT_TRACE_OBJ(IOT_EVT_SEM_GIVE, 3U, 4U);
    IOT_TRACE_DBG(IOT_EVT_TICK, 5U, 6U);
    TF_CHECK_INT(iot_trace_count(), 0);

    /* 2. 动态开启：三个等级全部生效 */
    iot_trace_enable();
    TF_CHECK_INT(iot_trace_get_level(), IOT_TRACE_LEVEL_DEBUG);
    IOT_TRACE_CORE(IOT_EVT_SWITCH, 0xAAU, 0xBBU);
    IOT_TRACE_OBJ(IOT_EVT_SEM_GIVE, 0xCCU, 0xDDU);
    IOT_TRACE_DBG(IOT_EVT_TICK, 7U, 1U);
    TF_CHECK_INT(iot_trace_count(), 3);

    ev = last_event();
    TF_CHECK(ev != NULL);
    if (ev != NULL) {
        TF_CHECK_INT(ev->id, IOT_EVT_TICK);
        TF_CHECK_INT(ev->value, 7);
        TF_CHECK_INT(ev->arg, 1);
        TF_CHECK_INT(ev->seq, 2);
    }

    /* 3. 等级过滤：CORE 级只记录 CORE 宏 */
    iot_trace_reset();
    iot_trace_set_level(IOT_TRACE_LEVEL_CORE);
    IOT_TRACE_CORE(IOT_EVT_TASK_WAKE, 1U, 0U);
    IOT_TRACE_OBJ(IOT_EVT_SEM_TAKE, 1U, 0U);
    IOT_TRACE_DBG(IOT_EVT_TICK, 1U, 1U);
    TF_CHECK_INT(iot_trace_count(), 1);

    /* 4. OBJ 级放行 CORE + OBJ，仍过滤 DBG */
    iot_trace_reset();
    iot_trace_set_level(IOT_TRACE_LEVEL_OBJ);
    IOT_TRACE_CORE(IOT_EVT_TASK_BLOCK, 1U, 0U);
    IOT_TRACE_OBJ(IOT_EVT_QUEUE_SEND, 2U, 0U);
    IOT_TRACE_DBG(IOT_EVT_TASK_READY, 3U, 0U);
    TF_CHECK_INT(iot_trace_count(), 2);

    /* 5. 运行期关掉后立刻不再产生事件 */
    iot_trace_disable();
    IOT_TRACE(IOT_EVT_MARKER, 9U, 9U);
    TF_CHECK_INT(iot_trace_count(), 2);

    /* 6. 自定义时钟生效 */
    iot_trace_reset();
    iot_trace_set_clock(test_clock);
    iot_trace_enable();
    IOT_TRACE(IOT_EVT_MARKER, 0U, 0U);
    ev = last_event();
    TF_CHECK(ev != NULL);
    if (ev != NULL) {
        TF_CHECK_INT(ev->ts, 0x12345678U);
    }
    iot_trace_set_clock(NULL);

    /* 7. barectf CTF 包经 sink 导出 */
    s_sink_len = 0U;
    s_sink_calls = 0U;
    memset(s_sink_buf, 0, sizeof(s_sink_buf));
    iot_trace_set_sink(test_sink, NULL);
    iot_trace_reset();
    for (i = 0U; i < 4U; i++) {
        IOT_TRACE(IOT_EVT_SWITCH, i + 1U, i);
    }
    TF_CHECK_INT(iot_trace_flush(), IOT_OK);
    TF_CHECK(s_sink_calls > 0U);
    TF_CHECK(s_sink_len >= 4U);
    if (s_sink_len >= 4U) {
        TF_CHECK_INT(s_sink_buf[0], CTF_MAGIC_0);
        TF_CHECK_INT(s_sink_buf[1], CTF_MAGIC_1);
        TF_CHECK_INT(s_sink_buf[2], CTF_MAGIC_2);
        TF_CHECK_INT(s_sink_buf[3], CTF_MAGIC_3);
    }
    iot_trace_set_sink(NULL, NULL);

    /* 7b. PC 文件 sink：CTF 流真实落盘并带合法 magic */
    {
        uint8_t magic[4] = {0U, 0U, 0U, 0U};
        FILE *fp;

        iot_trace_reset();
        iot_trace_start_file("iot_trace_test_stream");
        for (i = 0U; i < 2U; i++) {
            IOT_TRACE(IOT_EVT_SWITCH, i, 0U);
        }
        iot_trace_stop_file();

        fp = fopen("iot_trace_test_stream", "rb");
        TF_CHECK(fp != NULL);
        if (fp != NULL) {
            TF_CHECK_INT(fread(magic, 1U, sizeof(magic), fp), 4);
            (void)fclose(fp);
        }
        (void)remove("iot_trace_test_stream");
        TF_CHECK_INT(magic[0], CTF_MAGIC_0);
        TF_CHECK_INT(magic[1], CTF_MAGIC_1);
        TF_CHECK_INT(magic[2], CTF_MAGIC_2);
        TF_CHECK_INT(magic[3], CTF_MAGIC_3);
    }

    /* 8. 环形覆盖：超过容量后计数继续增长，槽位可回绕读取 */
    iot_trace_reset();
    for (i = 0U; i < iot_trace_capacity() + 8U; i++) {
        IOT_TRACE(IOT_EVT_MARKER, i, 0U);
    }
    TF_CHECK_INT(iot_trace_count(), iot_trace_capacity() + 8U);
    ev = last_event();
    TF_CHECK(ev != NULL);
    if (ev != NULL) {
        TF_CHECK_INT(ev->value, iot_trace_capacity() + 7U);
    }
    TF_CHECK_INT(iot_trace_dropped(), 0);

    /* 收尾：恢复默认关闭，避免影响后续测试 */
    iot_trace_disable();
    iot_trace_reset();

    tf_end();
#else
    tf_begin("iot/trace (编译期裁剪)");
    iot_trace_init();
    IOT_TRACE(IOT_EVT_MARKER, 1U, 2U);
    TF_CHECK_INT(iot_trace_count(), 0);
    tf_end();
#endif
}
