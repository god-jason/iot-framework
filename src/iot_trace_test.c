/*
 * iot_trace_test.c - 事件跟踪回归测试（流式文本行模型）
 *
 * 覆盖：运行期动态开关、宏等级过滤、冻结、名称映射、µs 时钟、
 *       ISR 标记编码，以及 PC 平台产出的文本行内容。
 */

#include "test.h"
#include "iot.h"

#include <stdio.h>
#include <string.h>

#define TRACE_FILE "iot_trace_test.log"
#define DEFAULT_TRACE_FILE "iot_trace.log"

static size_t read_file(const char *path, char *buf, size_t cap)
{
    FILE *fp = fopen(path, "rb");
    size_t n = 0U;

    if (fp != NULL) {
        n = fread(buf, 1U, cap - 1U, fp);
        (void)fclose(fp);
    }
    buf[n] = '\0';
    return n;
}

static size_t read_trace(char *buf, size_t cap)
{
    return read_file(TRACE_FILE, buf, cap);
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

    /* 9. 停止输出：文件内容为文本行 */
    iot_trace_stop_file();
    n = read_trace(out, sizeof(out));
    TF_CHECK(n > 0U);
    TF_CHECK_INT(out[0], 'T');
    TF_CHECK(strstr(out, " switch ") != NULL);
    TF_CHECK(strstr(out, " sem_give ") != NULL);
    TF_CHECK(strstr(out, " name_begin ") != NULL);
    TF_CHECK(strstr(out, "task=0x") != NULL);
    TF_CHECK(strstr(out, "v1=") != NULL);
    TF_CHECK(strstr(out, "v2=") != NULL);
    /* 非中断上下文产生的事件不带 isr 标记 */
    TF_CHECK(strstr(out, "isr=1") == NULL);
    /* 自定义 µs 时钟写入行首时间戳（0x12345678 = 305419896） */
    TF_CHECK(strstr(out, "T 305419896 ") != NULL);
    /* 每行以 CRLF 结束 */
    TF_CHECK_INT(out[n - 2U], '\r');
    TF_CHECK_INT(out[n - 1U], '\n');
    (void)remove(TRACE_FILE);

    /* 9b. 未指定文件时，PC 端默认写入 iot_trace.log */
    (void)remove(DEFAULT_TRACE_FILE);
    iot_trace_start_file(NULL);
    iot_trace_set_level(IOT_TRACE_LEVEL_CORE);
    IOT_TRACE_CORE(IOT_EVT_TICK, 0U, 0U);
    iot_trace_stop_file();
    n = read_file(DEFAULT_TRACE_FILE, out, sizeof(out));
    TF_CHECK(n > 0U);
    TF_CHECK(strstr(out, " tick ") != NULL);
    (void)remove(DEFAULT_TRACE_FILE);

    /* 10. ISR 标记编码与事件名表 */
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
