/*
 * iot_crash_test.c - 崩溃记录模块单元测试
 *
 * 覆盖：记录/读取/清除、CRC 校验、同启动只保留首次、文本格式化与截断、
 *       原因名称映射、参数防御。
 * 不触发真实故障信号：那会终止测试进程（故障捕获本身由平台层负责）。
 */

#include "test.h"
#include "iot_crash.h"

#include <string.h>

void ut_crash(void)
{
    char buf[1024];
    char small[40];
    const iot_crash_record_t *rec;
    iot_crash_context_t ctx;
    int n;

    tf_begin("iot/crash 记录与格式化");

    iot_crash_clear();
    TF_CHECK_INT(iot_crash_pending(), 0);
    TF_CHECK(iot_crash_get() == NULL);

    /* -------- 记录一次 HardFault 现场 -------- */
    memset(&ctx, 0, sizeof(ctx));
    ctx.regs[0] = 1U;
    ctx.regs[1] = 2U;
    ctx.regs[2] = 3U;
    ctx.regs[3] = 4U;
    ctx.regs[4] = 0x0000000CU;
    ctx.regs[5] = 0x08005678U;    /* LR */
    ctx.regs[6] = 0x08001234U;    /* PC */
    ctx.regs[7] = 0x61000000U;    /* xPSR */
    ctx.sp = 0x20004F00U;
    ctx.cfsr = 0x00008200U;
    ctx.hfsr = 0x40000000U;
    ctx.exc_return = 0xFFFFFFFDU;
    ctx.file = "src/pid.c";
    ctx.line = 42U;
    ctx.expr = "pid != NULL";

    TF_CHECK_INT(iot_crash_record(IOT_CRASH_HARD_FAULT, &ctx), 0);
    TF_CHECK_INT(iot_crash_pending(), 1);

    rec = iot_crash_get();
    TF_CHECK(rec != NULL);
    if (rec != NULL) {
        TF_CHECK_INT(rec->magic, IOT_CRASH_MAGIC);
        TF_CHECK_INT(rec->version, IOT_CRASH_VERSION);
        TF_CHECK_INT(rec->reason, IOT_CRASH_HARD_FAULT);
        TF_CHECK_INT(rec->regs[6], 0x08001234U);
        TF_CHECK_INT(rec->regs[5], 0x08005678U);
        TF_CHECK_INT(rec->sp, 0x20004F00U);
        TF_CHECK_INT(rec->cfsr, 0x00008200U);
        TF_CHECK_INT(rec->line, 42U);
        TF_CHECK_STR(rec->file, "src/pid.c");
        TF_CHECK_STR(rec->expr, "pid != NULL");
        TF_CHECK(rec->seq >= 1U);
        TF_CHECK(rec->build[0] != '\0');
        TF_CHECK_STR(iot_crash_reason_name((int)rec->reason), "HARD_FAULT");
    }

    /* -------- 同一次启动只保留第一次现场（根因） -------- */
    TF_CHECK_INT(iot_crash_record(IOT_CRASH_ASSERT, NULL), 0);
    rec = iot_crash_get();
    TF_CHECK(rec != NULL && rec->reason == IOT_CRASH_HARD_FAULT);
    TF_CHECK(rec != NULL && rec->line == 42U);

    /* -------- 文本化 -------- */
    n = iot_crash_format(rec, buf, sizeof(buf));
    TF_CHECK(n > 0);
    TF_CHECK(strstr(buf, "HARD_FAULT") != NULL);
    TF_CHECK(strstr(buf, "08001234") != NULL);
    TF_CHECK(strstr(buf, "src/pid.c") != NULL);
    TF_CHECK(strstr(buf, "pid != NULL") != NULL);
    TF_CHECK(strstr(buf, "seq=") != NULL);
    TF_CHECK(strstr(buf, "cfsr=00008200") != NULL);
    TF_CHECK(strstr(buf, "CRASH v") != NULL);

    /* 小缓冲截断：不得越界，且仍以 '\0' 结尾 */
    memset(small, 0x5A, sizeof(small));
    n = iot_crash_format(rec, small, sizeof(small));
    TF_CHECK(n >= 0 && n < (int)sizeof(small));
    TF_CHECK(small[sizeof(small) - 1U] == '\0');

    /* -------- 参数防御与名称映射 -------- */
    TF_CHECK_INT(iot_crash_format(NULL, buf, sizeof(buf)), -1);
    TF_CHECK_INT(iot_crash_format(rec, NULL, sizeof(buf)), -1);
    TF_CHECK_STR(iot_crash_reason_name(99), "UNKNOWN");
    TF_CHECK_STR(iot_crash_reason_name(IOT_CRASH_ASSERT), "ASSERT");
    TF_CHECK_STR(iot_crash_reason_name(IOT_CRASH_STACK_OVERFLOW),
                 "STACK_OVERFLOW");

    /* -------- CRC 被破坏：init 应判为无效并清空 -------- */
    ((iot_crash_record_t *)rec)->crc ^= 0xFFFFFFFFU;
    iot_crash_init();
    TF_CHECK_INT(iot_crash_pending(), 0);
    TF_CHECK(iot_crash_get() == NULL);

    /* -------- 清除后允许重新记录 -------- */
    iot_crash_clear();
    TF_CHECK_INT(iot_crash_record(IOT_CRASH_ASSERT, NULL), 0);
    TF_CHECK_INT(iot_crash_pending(), 1);
    rec = iot_crash_get();
    TF_CHECK(rec != NULL && rec->reason == IOT_CRASH_ASSERT);

    /* 非法原因被拒绝，且不破坏已有记录 */
    TF_CHECK_INT(iot_crash_record(IOT_CRASH_NONE, NULL), -1);
    TF_CHECK_INT(iot_crash_pending(), 1);

    iot_crash_clear();
    TF_CHECK_INT(iot_crash_pending(), 0);

    tf_end();
}
