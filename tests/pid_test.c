/* algorithm/control 单元测试：PID 比例/积分/微分、限幅、复位与增量式。 */
#include "test_framework.h"
#include "pid.h"

void ut_pid(void)
{
    pid_t pid;
    pid_incremental_t inc;

    tf_begin("algorithm/pid 初始化与默认限幅");
    memset(&pid, 0, sizeof(pid));
    TF_CHECK_INT(pid_init(&pid, 2.0f, 1.0f, 0.5f), 0);
    TF_CHECK_NEAR(pid.kp, 2.0f, 1e-6);
    TF_CHECK_NEAR(pid.ki, 1.0f, 1e-6);
    TF_CHECK_NEAR(pid.kd, 0.5f, 1e-6);
    TF_CHECK_NEAR(pid.output_min, -1000000.0f, 1e-3);
    TF_CHECK_NEAR(pid.output_max, 1000000.0f, 1e-3);
    TF_CHECK(!pid.initialized);
    TF_CHECK_INT(pid_init(NULL, 1.0f, 0.0f, 0.0f), -1);
    tf_end();

    tf_begin("algorithm/pid 纯比例");
    pid_init(&pid, 2.0f, 0.0f, 0.0f);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 1.0f), 20.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, 5.0f, 1.0f, 1.0f), 8.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, 0.0f, 0.0f, 1.0f), 0.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/pid 积分作用");
    pid_init(&pid, 0.0f, 1.0f, 0.0f);
    /* 首次调用只建立状态，不累加积分 */
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 0.5f), 0.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 0.5f), 5.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 0.5f), 10.0f, 1e-4);
    TF_CHECK_NEAR(pid.integral, 10.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/pid 微分作用");
    pid_init(&pid, 0.0f, 0.0f, 1.0f);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 0.5f), 0.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, 20.0f, 0.0f, 0.5f), 20.0f, 1e-4);
    /* 误差不变时微分为 0 */
    TF_CHECK_NEAR(pid_update(&pid, 20.0f, 0.0f, 0.5f), 0.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/pid 输出限幅");
    pid_init(&pid, 100.0f, 0.0f, 0.0f);
    pid_set_limits(&pid, -5.0f, 5.0f);
    TF_CHECK_NEAR(pid_update(&pid, 100.0f, 0.0f, 1.0f), 5.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, -100.0f, 0.0f, 1.0f), -5.0f, 1e-4);
    pid_set_limits(NULL, 0.0f, 1.0f);
    tf_end();

    tf_begin("algorithm/pid 复位与参数修改");
    pid_init(&pid, 1.0f, 1.0f, 0.0f);
    pid_update(&pid, 10.0f, 0.0f, 1.0f);
    pid_update(&pid, 10.0f, 0.0f, 1.0f);
    pid_reset(&pid);
    TF_CHECK_NEAR(pid.output, 0.0f, 1e-6);
    TF_CHECK_NEAR(pid.integral, 0.0f, 1e-6);
    TF_CHECK(!pid.initialized);
    pid_set_params(&pid, 3.0f, 0.0f, 0.0f);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 1.0f), 30.0f, 1e-4);
    pid_reset(NULL);
    pid_set_params(NULL, 1.0f, 1.0f, 1.0f);
    tf_end();

    tf_begin("algorithm/pid 非正周期与空指针");
    pid_init(&pid, 1.0f, 0.0f, 0.0f);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 1.0f), 10.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, 10.0f, 0.0f, 0.0f), 10.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(&pid, 100.0f, 0.0f, -1.0f), 10.0f, 1e-4);
    TF_CHECK_NEAR(pid_update(NULL, 1.0f, 0.0f, 1.0f), 0.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/pid 增量式");
    memset(&inc, 0, sizeof(inc));
    TF_CHECK_INT(pid_incremental_init(&inc, 1.0f, 0.0f, 0.0f), 0);
    TF_CHECK(!inc.initialized);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 10.0f, 0.0f), 0.0f, 1e-4);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 20.0f, 0.0f), 10.0f, 1e-4);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 20.0f, 0.0f), 10.0f, 1e-4);
    TF_CHECK_INT(pid_incremental_init(NULL, 1.0f, 0.0f, 0.0f), -1);
    tf_end();

    tf_begin("algorithm/pid 增量式积分项");
    pid_incremental_init(&inc, 0.0f, 1.0f, 0.0f);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 10.0f, 0.0f), 0.0f, 1e-4);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 10.0f, 0.0f), 10.0f, 1e-4);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 10.0f, 0.0f), 20.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/pid 增量式微分项");
    pid_incremental_init(&inc, 0.0f, 0.0f, 1.0f);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 10.0f, 0.0f), 0.0f, 1e-4);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 10.0f, 0.0f), 0.0f, 1e-4);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 30.0f, 0.0f), 20.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/pid 增量式限幅与复位");
    pid_incremental_init(&inc, 1.0f, 0.0f, 0.0f);
    pid_incremental_set_limits(&inc, -5.0f, 5.0f);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 10.0f, 0.0f), 0.0f, 1e-4);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 100.0f, 0.0f), 5.0f, 1e-4);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 0.0f, 0.0f), -5.0f, 1e-4);
    pid_incremental_reset(&inc);
    TF_CHECK_NEAR(inc.output, 0.0f, 1e-6);
    TF_CHECK(!inc.initialized);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 100.0f, 0.0f), 0.0f, 1e-4);
    TF_CHECK_NEAR(pid_incremental_update(&inc, 200.0f, 0.0f), 5.0f, 1e-4);
    tf_end();

    tf_begin("algorithm/pid 增量式空指针");
    TF_CHECK_NEAR(pid_incremental_update(NULL, 1.0f, 0.0f), 0.0f, 1e-4);
    pid_incremental_reset(NULL);
    pid_incremental_set_limits(NULL, -1.0f, 1.0f);
    tf_end();
}
