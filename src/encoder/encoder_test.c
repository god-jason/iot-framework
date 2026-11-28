/*
 * encoder_test.c - 编码器测试（对应 encoder.c）
 *
 * 覆盖：初始化校验、计数读取、转速差分、复位基准、异常参数。
 */

#include "test.h"
#include "encoder.h"

static int s_count;
static int s_read_fail;
static int s_reset_count;

static int sim_read_count(void *ctx, int32_t *count)
{
    (void)ctx;
    if (s_read_fail) {
        return -1;
    }
    *count = s_count;
    return 0;
}

static int sim_reset(void *ctx)
{
    (void)ctx;
    s_reset_count++;
    s_count = 0;
    return 0;
}

void ut_encoder(void)
{
    encoder_t enc;
    encoder_ops_t ops;
    int32_t count = 0;
    float speed = 0.0f;

    ops.read_count = sim_read_count;
    ops.reset = sim_reset;
    ops.ctx = NULL;

    s_count = 0;
    s_read_fail = 0;
    s_reset_count = 0;

    tf_begin("encoder/初始化");

    TF_CHECK_INT(encoder_init(NULL, &ops), -1);
    TF_CHECK_INT(encoder_init(&enc, NULL), -1);
    {
        encoder_ops_t bad;
        bad.read_count = NULL;
        bad.reset = NULL;
        bad.ctx = NULL;
        TF_CHECK_INT(encoder_init(&enc, &bad), -1);
    }
    TF_CHECK_INT(encoder_init(&enc, &ops), 0);

    tf_end();

    tf_begin("encoder/计数与转速");

    s_count = 100;
    TF_CHECK_INT(encoder_read(&enc, &count), 0);
    TF_CHECK_INT(count, 100);
    TF_CHECK_INT(enc.count, 100);

    /* 首次计算以 last_count 为基准；这里先同步基准 */
    enc.last_count = 100;
    s_count = 150;
    TF_CHECK_INT(encoder_get_speed(&enc, 0.5f, &speed), 0);
    TF_CHECK_NEAR(speed, 100.0f, 1e-3);       /* 50 / 0.5 */
    TF_CHECK_INT(enc.last_count, 150);

    /* dt <= 0 应失败 */
    TF_CHECK_INT(encoder_get_speed(&enc, 0.0f, &speed), -1);
    TF_CHECK_INT(encoder_get_speed(&enc, -1.0f, &speed), -1);

    tf_end();

    tf_begin("encoder/复位与失败路径");

    s_count = 42;
    TF_CHECK_INT(encoder_read(&enc, &count), 0);
    TF_CHECK_INT(encoder_reset(&enc), 0);
    TF_CHECK_INT(s_reset_count, 1);
    /* reset 先同步差分基准，再调用硬件复位 */
    TF_CHECK_INT(enc.last_count, 42);
    TF_CHECK_INT(enc.count, 42);

    s_read_fail = 1;
    TF_CHECK_INT(encoder_read(&enc, &count), -1);
    s_read_fail = 0;

    TF_CHECK_INT(encoder_read(NULL, &count), -1);
    TF_CHECK_INT(encoder_read(&enc, NULL), -1);
    TF_CHECK_INT(encoder_reset(NULL), -1);
    TF_CHECK_INT(encoder_get_speed(&enc, 1.0f, NULL), -1);

    tf_end();
}
