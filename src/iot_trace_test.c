/*
 * iot_trace_test.c - 事件跟踪回归测试（流式输出模型）
 *
 * 覆盖：运行期动态开关、宏等级过滤、冻结、名称映射、µs 时钟、
 *       ISR 标记编码，以及 PC 平台直接产出的 JSON 内容。
 */

#include "test.h"
#include "iot.h"

#include <stdio.h>
#include <string.h>

#define TRACE_FILE "iot_trace_test.json"

static size_t read_trace(char *buf, size_t cap)
{
    FILE *fp = fopen(TRACE_FILE, "rb");
    size_t n = 0U;

    if (fp != NULL) {
        n = fread(buf, 1U, cap - 1U, fp);
        (void)fclose(fp);
    }
    buf[n] = '\0';
    return n;
}

static uint32_t test_clock(void)
{
    return 0x12345678U;
}

void ut_trace(void)
{
#if IOT_USE_TRACE
    char out[8192];
    size_t n;

    tf_begin("iot/trace 流式输出");

    iot_trace_init();
    iot_trace_start_file(TRACE_FILE);
    iot_trace_set_level(IOT_TRACE_LEVEL_OFF);

    /* 1. 关闭时：无事件、文件里没有任何输出 */
    IOT_TRACE(IOT_EVT_MARKER, 1U, 2U);
    IOT_TRACE_OBJ(IOT_EVT_SEM_GIVE, 3U, 4U);
    IOT_TRACE_DBG(IOT_EVT_TICK, 5U, 6U);
    TF_CHECK_INT(iot_trace_count(), 0);
    TF_CHECK_INT((int)read_trace(out, sizeof(out)), 0);

    /* 2. 运行期开启：三类宏全部发射 */
    iot_trace_enable();
    TF_CHECK_INT(iot_trace_get_level(), IOT_TRACE_LEVEL_DEBUG);
    IOT_TRACE_CORE(IOT_EVT_SWITCH, 0xAAU, 0xBBU);
    IOT_TRACE_OBJ(IOT_EVT_SEM_GIVE, 0xCCU, 0xDDU);
    IOT_TRACE_DBG(IOT_EVT_TICK, 7U, 1U);
    TF_CHECK_INT(iot_trace_count(), 3);

    /* 3. 等级过滤：CORE 级只留 CORE 宏 */
    iot_trace_set_level(IOT_TRACE_LEVEL_CORE);
    IOT_TRACE_CORE(IOT_EVT_TASK_WAKE, 1U, 0U);
    IOT_TRACE_OBJ(IOT_EVT_SEM_TAKE, 1U, 0U);
    IOT_TRACE_DBG(IOT_EVT_TICK, 1U, 1U);
    TF_CHECK_INT(iot_trace_count(), 4);

    /* 4. OBJ 级放行 CORE + OBJ */
    iot_trace_set_level(IOT_TRACE_LEVEL_OBJ);
    IOT_TRACE_CORE(IOT_EVT_TASK_BLOCK, 1U, 0U);
    IOT_TRACE_OBJ(IOT_EVT_QUEUE_SEND, 2U, 0U);
    IOT_TRACE_DBG(IOT_EVT_TASK_READY, 3U, 0U);
    TF_CHECK_INT(iot_trace_count(), 6);

    /* 5. 关闭等级后立刻停止发射 */
    iot_trace_set_level(IOT_TRACE_LEVEL_OFF);
    IOT_TRACE(IOT_EVT_MARKER, 9U, 9U);
    TF_CHECK_INT(iot_trace_count(), 6);

    /* 6. 冻结：后续事件被丢弃 */
    iot_trace_set_level(IOT_TRACE_LEVEL_DEBUG);
    iot_trace_freeze();
    TF_CHECK(iot_trace_is_frozen());
    IOT_TRACE(IOT_EVT_MARKER, 1U, 2U);
    TF_CHECK_INT(iot_trace_count(), 6);
    TF_CHECK_INT(iot_trace_dropped(), 1);
    iot_trace_unfreeze();
    TF_CHECK(!iot_trace_is_frozen());
    IOT_TRACE(IOT_EVT_MARKER, 1U, 2U);
    TF_CHECK_INT(iot_trace_count(), 7);

    /* 7. 名称映射：1 条 BEGIN + 2 条 CHUNK（"blink" 5 字节） */
    iot_trace_name((const void *)(uintptr_t)0x1234U,
                   IOT_TRACE_NAME_TASK, "blink");
    TF_CHECK_INT(iot_trace_count(), 10);

    /* 8. 自定义 µs 时钟写入 ts 字段 */
    iot_trace_set_clock(test_clock);
    IOT_TRACE(IOT_EVT_MARKER, 0U, 0U);
    iot_trace_set_clock(NULL);
    TF_CHECK_INT(iot_trace_count(), 11);

    /* 9. 停止输出：文件里是一个完整合法的 JSON 数组 */
    iot_trace_stop_file();
    n = read_trace(out, sizeof(out));
    TF_CHECK(n > 0U);
    TF_CHECK_INT(out[0], '[');
    TF_CHECK(strstr(out, "\"name\":\"switch\"") != NULL);
    TF_CHECK(strstr(out, "\"name\":\"sem_give\"") != NULL);
    TF_CHECK(strstr(out, "\"name\":\"name_begin\"") != NULL);
    TF_CHECK(strstr(out, "\"ph\":\"i\"") != NULL);
    TF_CHECK(strstr(out, "\"isr\":0") != NULL);
    TF_CHECK(strstr(out, "\"ts\":305419896") != NULL);  /* 0x12345678 */
    TF_CHECK_INT(out[n - 2U], ']');
    (void)remove(TRACE_FILE);

    /* 10. 启用 CTF 后端时同时输出 CTF 文件（二进制流 + metadata 侧车文件） */
#if IOT_TRACE_BACKEND_BARECTF
    {
        const char *ctf = "iot_trace_test.ctf";
        char meta_path[64];
        uint8_t head[4];
        FILE *fp;
        long size;
        char md[8192];
        size_t mdn;

        iot_trace_set_level(IOT_TRACE_LEVEL_DEBUG);
        iot_trace_start_ctf_file(ctf);
        IOT_TRACE_CORE(IOT_EVT_SWITCH, 0x11U, 0x22U);
        IOT_TRACE_OBJ(IOT_EVT_SEM_GIVE, 0x33U, 0x44U);
        IOT_TRACE_DBG(IOT_EVT_TICK, 1U, 1U);
        iot_trace_stop_ctf_file();

        /* 二进制流文件：CTF magic 0xC1 1F FC C1 (LE)，且非空 */
        fp = fopen(ctf, "rb");
        TF_CHECK(fp != NULL);
        if (fp != NULL) {
            size = 0; (void)fseek(fp, 0, SEEK_END); size = ftell(fp);
            (void)fseek(fp, 0, SEEK_SET);
            TF_CHECK(size > 4L);
            TF_CHECK_INT((int)fread(head, 1U, sizeof(head), fp), 4);
            (void)fclose(fp);
        }
        TF_CHECK_INT(head[0], 0xC1U);
        TF_CHECK_INT(head[1], 0x1FU);
        TF_CHECK_INT(head[2], 0xFCU);
        TF_CHECK_INT(head[3], 0xC1U);

        /* metadata 侧车文件存在且含语义事件类型 */
        (void)snprintf(meta_path, sizeof(meta_path), "%s.metadata", ctf);
        fp = fopen(meta_path, "rb");
        TF_CHECK(fp != NULL);
        mdn = 0U;
        if (fp != NULL) {
            mdn = fread(md, 1U, sizeof(md) - 1U, fp);
            (void)fclose(fp);
        }
        TF_CHECK(mdn > 0U);
        md[mdn] = '\0';
        TF_CHECK(strstr(md, "sched_switch") != NULL);

        (void)remove(ctf);
        (void)remove(meta_path);
        iot_trace_set_level(IOT_TRACE_LEVEL_OFF);
    }
#endif

    /* 11. ISR 标记编码与事件名表 */
    TF_CHECK(IOT_TRACE_IN_ISR(IOT_EVT_SWITCH | IOT_TRACE_FLAG_ISR));
    TF_CHECK(!IOT_TRACE_IN_ISR(IOT_EVT_SWITCH));
    TF_CHECK_INT(IOT_TRACE_SEQ(IOT_EVT_SWITCH | IOT_TRACE_FLAG_ISR), 0);
    TF_CHECK_INT(IOT_TRACE_GROUP(IOT_EVT_SWITCH | IOT_TRACE_FLAG_ISR),
                 IOT_TRACE_GRP_CORE);
    TF_CHECK_INT(strcmp(iot_trace_event_name(IOT_EVT_SWITCH), "switch"), 0);
    TF_CHECK_INT(strcmp(iot_trace_event_name(IOT_EVT_TASK_SUSPEND),
                        "task_suspend"), 0);

    /* 收尾：恢复默认关闭 */
    iot_trace_disable();

    tf_end();
#else
    tf_begin("iot/trace (编译期裁剪)");
    iot_trace_init();
    IOT_TRACE(IOT_EVT_MARKER, 1U, 2U);
    TF_CHECK_INT(iot_trace_count(), 0);
    tf_end();
#endif
}
