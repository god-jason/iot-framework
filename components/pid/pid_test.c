/*
 * pid_test.c - PID 控制测试（对应 pid.c）
 *
 * 覆盖：位置式的比例/积分/微分/限幅/复位、参数在线调整，
 *       增量式的累加输出与边界。
 */

#include "test.h"
#include "pid.h"

void ut_pid(void)
{
    pid_t pid;
    pid_incremental_t inc;

    tf_begin("pid/初始化与默认值");

    TF_CHECK_INT(pid_init(NULL, 1.0f, 0.0f, 0.0f), -1);
    TF_CHECK_INT(pid_init(&pid, 2.0f, 1.0f, 0.5f), 0);
    TF_CHECK_NEAR(pid.kp, 2.0f, 1e-6);
    TF_CHECK_NEAR(pid.ki, 1.0f, 1e-6);
    TF_CHECK_NEAR(pid.kd, 0.5f, 1e-6);
    TF_CHECK(!pid.initialized);
    TF_CHECK_NEAR(pid.output, 0.0f, 1e-6);

    tf_end();

    tf_begin("pid/纯比例与限幅");

    pid_init(&pid, 2.0f, 0.0f, 0.0f);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 1.0f), 20.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, 5.0f, 1.0f, 1.0f), 8.0f, 1e-4);

    pid_set_limits(&pid, -5.0f, 5.0f);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 1.0f), 5.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, -10.0f, 0.0f, 1.0f), -5.0f, 1e-4);
    TF_CHECK_NEAR(pid.output, -5.0f, 1e-4);

    tf_end();

    tf_begin("pid/积分与微分");

    pid_init(&pid, 0.0f, 1.0f, 0.0f);
    /* 首次调用只建立状态，不累加积分 */
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 0.5f), 0.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 0.5f), 5.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 0.5f), 10.0f, 1e-4);
    TF_CHECK_NEAR(pid.integral, 10.0f, 1e-4);

    pid_init(&pid, 0.0f, 0.0f, 1.0f);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 0.5f), 0.0f, 1e-4);
    /* 误差从 10 变为 20：微分 = (20-10)/0.5 = 20 */
    TF_CHECK_NEAR(pid_update(&pid, 20.0f, 0.0f, 0.5f), 20.0f, 1e-4);
    /* 误差不变：微分为 0 */
    TF_CHECK_NEAR(pid_update(&pid, 20.0f, 0.0f, 0.5f), 0.0f, 1e-4);

    tf_end();

    tf_begin("pid/复位与参数调整");

    pid_init(&pid, 1.0f, 1.0f, 0.0f);
    (void)pid_update(&pid, 10.0f, 0.0f, 1.0f);   /* 首次只建立状态 */
    (void)pid_update(&pid, 10.0f, 0.0f, 1.0f);   /* 第二次开始累加积分 */
    TF_CHECK(pid.integral != 0.0f);
    pid_reset(&pid);
    TF_CHECK_NEAR(pid.integral, 0.0f, 1e-6);
    TF_CHECK(!pid.initialized);

    pid_set_params(&pid, 3.0f, 0.0f, 0.0f);
    TF_CHECK_NEAR(pid.kp, 3.0f, 1e-6);
    TF_CHECK_NEAR(pid_update(&pid, 1.0f, 0.0f, 1.0f), 3.0f, 1e-4);

    /* dt <= 0 返回上次输出 */
    {
        float last = pid_update(&pid, 1.0f, 0.0f, 1.0f);
        TF_CHECK_NEAR(pid_update(&pid, 5.0f, 0.0f, 0.0f), last, 1e-6);
    }

    pid_reset(NULL);
    pid_set_params(NULL, 1.0f, 0.0f, 0.0f);
    pid_set_limits(NULL, 0.0f, 1.0f);
    TF_CHECK_NEAR(pid_update(NULL, 1.0f, 0.0f, 1.0f), 0.0f, 1e-6);

    tf_end();

    tf_begin("pid/增量式");

    TF_CHECK_INT(pid_incremental_init(NULL, 1.0f, 0.0f, 0.0f), -1);
    TF_CHECK_INT(pid_incremental_init(&inc, 1.0f, 0.0f, 0.0f), 0);
    /* 首次只记录误差，不产生增量 */
    TF_CHECK_NEAR(pid_incremental_update(&inc, 10.0f, 0.0f), 0.0f, 1e-4);
    /* 误差保持不变：增量为 0，累计输出不变 */
    TF_CHECK_NEAR(pid_incremental_update(&inc, 10.0f, 0.0f), 0.0f, 1e-4);
    /* 误差减小：输出应在限幅内 */
    pid_incremental_set_limits(&inc, -100.0f, 100.0f);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 5.0f, 0.0f), -5.0f, 1e-4);

    pid_incremental_reset(&inc);
    TF_CHECK_NEAR(inc.output, 0.0f, 1e-6);
    TF_CHECK_NEAR(pid_incremental_update(NULL, 1.0f, 0.0f), 0.0f, 1e-6);

    tf_end();
}
