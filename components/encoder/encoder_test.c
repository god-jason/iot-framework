/* 编码器组件测试：计数读取、转速计算、复位与错误路径。 */
#include "test.h"
#include "encoder.h"

static int32_t s_count;
static int s_read_calls;
static int s_reset_calls;
static int s_read_fail;

static int fake_read(void *ctx, int32_t *count)
{
    (void)ctx;
    s_read_calls++;
    if (s_read_fail) {
        return -1;
    }
    *count = s_count;
    return 0;
}

static int fake_reset(void *ctx)
{
    (void)ctx;
    s_reset_calls++;
    s_count = 0;
    return 0;
}

static const encoder_ops_t s_ops = { fake_read, fake_reset, NULL };
static const encoder_ops_t s_ops_no_reset = { fake_read, NULL, NULL };

void ut_encoder(void)
{
    encoder_t enc;
    encoder_ops_t bad_ops;
    int32_t count;
    float speed;

    tf_begin("encoder/初始化参数校验");
    TF_CHECK_INT(encoder_init(NULL, &s_ops), -1);
    TF_CHECK_INT(encoder_init(&enc, NULL), -1);
    bad_ops.read_count = NULL;
    bad_ops.reset = NULL;
    bad_ops.ctx = NULL;
    TF_CHECK_INT(encoder_init(&enc, &bad_ops), -1);
    tf_end();

    tf_begin("encoder/初始化状态复位");
    s_count = 77;
    s_read_calls = 0;
    s_reset_calls = 0;
    s_read_fail = 0;
    TF_CHECK_INT(encoder_init(&enc, &s_ops), 0);
    TF_CHECK_INT(enc.count, 0);
    TF_CHECK_INT(enc.last_count, 0);
    TF_CHECK_NEAR(enc.speed, 0.0, 1e-6);
    TF_CHECK_INT(s_read_calls, 0);
    tf_end();

    tf_begin("encoder/读取计数");
    TF_CHECK_INT(encoder_read(NULL, &count), -1);
    TF_CHECK_INT(encoder_read(&enc, NULL), -1);

    s_count = 1234;
    count = 0;
    TF_CHECK_INT(encoder_read(&enc, &count), 0);
    TF_CHECK_INT(count, 1234);
    TF_CHECK_INT(enc.count, 1234);

    s_count = -567;
    TF_CHECK_INT(encoder_read(&enc, &count), 0);
    TF_CHECK_INT(count, -567);
    tf_end();

    tf_begin("encoder/读取失败传播");
    s_read_fail = 1;
    count = -1;
    TF_CHECK_INT(encoder_read(&enc, &count), -1);
    TF_CHECK_INT(count, -1);
    s_read_fail = 0;
    tf_end();

    tf_begin("encoder/转速计算");
    s_read_fail = 0;
    s_count = 0;
    TF_CHECK_INT(encoder_init(&enc, &s_ops), 0);

    s_count = 5;
    TF_CHECK_INT(encoder_get_speed(&enc, 0.1f, &speed), 0);
    TF_CHECK_NEAR(speed, 50.0, 1e-3);
    TF_CHECK_INT(enc.last_count, 5);
    TF_CHECK_NEAR(enc.speed, 50.0, 1e-3);

    s_count = 8;
    TF_CHECK_INT(encoder_get_speed(&enc, 0.1f, &speed), 0);
    TF_CHECK_NEAR(speed, 30.0, 1e-3);
    TF_CHECK_INT(enc.last_count, 8);

    /* 反向计数得到负转速：(0 - 8) / 1.0 = -8 */
    s_count = 0;
    TF_CHECK_INT(encoder_get_speed(&enc, 1.0f, &speed), 0);
    TF_CHECK_NEAR(speed, -8.0, 1e-3);

    /* 无增量时转速为 0 */
    TF_CHECK_INT(encoder_get_speed(&enc, 1.0f, &speed), 0);
    TF_CHECK_NEAR(speed, 0.0, 1e-6);
    tf_end();

    tf_begin("encoder/转速参数校验");
    TF_CHECK_INT(encoder_get_speed(NULL, 1.0f, &speed), -1);
    TF_CHECK_INT(encoder_get_speed(&enc, 1.0f, NULL), -1);
    TF_CHECK_INT(encoder_get_speed(&enc, 0.0f, &speed), -1);
    TF_CHECK_INT(encoder_get_speed(&enc, -1.0f, &speed), -1);
    tf_end();

    tf_begin("encoder/转速读取失败");
    s_read_fail = 1;
    TF_CHECK_INT(encoder_get_speed(&enc, 1.0f, &speed), -1);
    s_read_fail = 0;
    tf_end();

    tf_begin("encoder/复位与基准更新");
    s_read_fail = 0;
    s_read_calls = 0;
    s_reset_calls = 0;
    s_count = 100;
    TF_CHECK_INT(encoder_init(&enc, &s_ops), 0);
    TF_CHECK_INT(encoder_read(&enc, &count), 0);
    TF_CHECK_INT(encoder_get_speed(&enc, 1.0f, &speed), 0);
    TF_CHECK_NEAR(speed, 100.0, 1e-3);
    TF_CHECK_INT(enc.last_count, 100);

    TF_CHECK_INT(encoder_reset(NULL), -1);
    TF_CHECK_INT(encoder_reset(&enc), 0);
    TF_CHECK_INT(s_reset_calls, 1);
    TF_CHECK_INT(enc.last_count, 100);
    TF_CHECK_INT(s_count, 0);

    /* 硬件计数被清零后，相对上次基准得到 -100 */
    TF_CHECK_INT(encoder_get_speed(&enc, 1.0f, &speed), 0);
    TF_CHECK_NEAR(speed, -100.0, 1e-3);
    tf_end();

    tf_begin("encoder/无硬件复位回调时的复位");
    s_reset_calls = 0;
    s_count = 42;
    TF_CHECK_INT(encoder_init(&enc, &s_ops_no_reset), 0);
    TF_CHECK_INT(encoder_read(&enc, &count), 0);
    TF_CHECK_INT(count, 42);
    TF_CHECK_INT(encoder_reset(&enc), 0);
    TF_CHECK_INT(s_reset_calls, 0);
    TF_CHECK_INT(enc.last_count, 42);
    tf_end();
}
