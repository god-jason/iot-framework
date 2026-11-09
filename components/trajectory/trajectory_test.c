/* trajectory 单元测试：梯形、S 曲线、多轴直线插补。 */
#include "test.h"
#include "trajectory.h"

void ut_trajectory(void)
{
    trajectory_trap_t trap;
    trajectory_scurve_t scurve;
    float pos;
    float vel;
    float acc;
    float start[2];
    float end[2];
    float out[2];
    float last;
    int i;

    tf_begin("trajectory/梯形规划与采样");
    TF_CHECK_INT(trajectory_trap_plan(NULL, 10.0f, 2.0f, 1.0f, 1.0f), -1);
    TF_CHECK_INT(trajectory_trap_plan(&trap, 0.0f, 2.0f, 1.0f, 1.0f), -1);
    TF_CHECK_INT(trajectory_trap_plan(&trap, 10.0f, 2.0f, 1.0f, 1.0f), 0);
    TF_CHECK_NEAR(trap.vpeak, 2.0f, 1e-5);
    TF_CHECK_NEAR(trap.t_acc, 2.0f, 1e-5);
    TF_CHECK_NEAR(trap.t_cruise, 3.0f, 1e-5);
    TF_CHECK_NEAR(trap.t_total, 7.0f, 1e-5);

    TF_CHECK_INT(trajectory_trap_sample(&trap, 0.0f, &pos, &vel, &acc), 0);
    TF_CHECK_NEAR(pos, 0.0f, 1e-6);
    TF_CHECK_NEAR(vel, 0.0f, 1e-6);
    TF_CHECK_NEAR(acc, 1.0f, 1e-6);

    TF_CHECK_INT(trajectory_trap_sample(&trap, 3.0f, &pos, &vel, &acc), 0);
    TF_CHECK_NEAR(pos, 4.0f, 1e-5);
    TF_CHECK_NEAR(vel, 2.0f, 1e-5);

    TF_CHECK_INT(trajectory_trap_sample(&trap, 7.0f, &pos, &vel, &acc), 0);
    TF_CHECK_NEAR(pos, 10.0f, 1e-5);
    TF_CHECK_NEAR(vel, 0.0f, 1e-5);
    tf_end();

    tf_begin("trajectory/梯形短行程降速");
    TF_CHECK_INT(trajectory_trap_plan(&trap, 0.5f, 2.0f, 1.0f, 1.0f), 0);
    TF_CHECK_NEAR(trap.vpeak, 0.7071068f, 1e-4);
    TF_CHECK_NEAR(trap.t_cruise, 0.0f, 1e-6);
    TF_CHECK_NEAR(trap.t_total, 1.4142136f, 1e-4);
    trajectory_trap_sample(&trap, trap.t_total, &pos, NULL, NULL);
    TF_CHECK_NEAR(pos, 0.5f, 1e-5);
    tf_end();

    tf_begin("trajectory/S 曲线规划与端点");
    TF_CHECK_INT(trajectory_scurve_plan(NULL, 10.0f, 2.0f, 4.0f, 20.0f), -1);
    TF_CHECK_INT(trajectory_scurve_plan(&scurve, 10.0f, 2.0f, 4.0f, 20.0f), 0);
    TF_CHECK(scurve.t_total > 0.0f);
    TF_CHECK_NEAR(scurve.vpeak, 2.0f, 1e-4);

    trajectory_scurve_sample(&scurve, 0.0f, &pos, &vel, &acc);
    TF_CHECK_NEAR(pos, 0.0f, 1e-6);
    TF_CHECK_NEAR(vel, 0.0f, 1e-6);

    trajectory_scurve_sample(&scurve, scurve.t_total, &pos, &vel, NULL);
    TF_CHECK_NEAR(pos, 10.0f, 1e-4);
    TF_CHECK_NEAR(vel, 0.0f, 1e-4);

    /* 全段单调递增且不超过峰值速度 */
    last = -1.0f;
    for (i = 0; i <= 100; i++) {
        float t = scurve.t_total * (float)i / 100.0f;
        trajectory_scurve_sample(&scurve, t, &pos, &vel, NULL);
        TF_CHECK(pos >= last - 1e-5f);
        TF_CHECK(vel <= scurve.vpeak + 1e-3f);
        last = pos;
    }
    tf_end();

    tf_begin("trajectory/S 曲线短行程");
    TF_CHECK_INT(trajectory_scurve_plan(&scurve, 0.2f, 2.0f, 4.0f, 20.0f), 0);
    TF_CHECK(scurve.vpeak < 2.0f);
    TF_CHECK_NEAR(scurve.t_cruise, 0.0f, 1e-6);
    trajectory_scurve_sample(&scurve, scurve.t_total, &pos, &vel, NULL);
    TF_CHECK_NEAR(pos, 0.2f, 1e-4);
    TF_CHECK_NEAR(vel, 0.0f, 1e-4);
    tf_end();

    tf_begin("trajectory/多轴直线插补");
    TF_CHECK_INT(trajectory_linear_plan(&trap, 1.0f, 1.0f, 1.0f), 0);
    start[0] = 0.0f; start[1] = 0.0f;
    end[0] = 2.0f;   end[1] = 4.0f;

    TF_CHECK_INT(trajectory_linear_sample(&trap, 0.0f, start, end, 2, out), 0);
    TF_CHECK_NEAR(out[0], 0.0f, 1e-6);
    TF_CHECK_NEAR(out[1], 0.0f, 1e-6);

    TF_CHECK_INT(trajectory_linear_sample(&trap, trap.t_total,
                                          start, end, 2, out), 0);
    TF_CHECK_NEAR(out[0], 2.0f, 1e-5);
    TF_CHECK_NEAR(out[1], 4.0f, 1e-5);

    TF_CHECK_INT(trajectory_linear_sample(&trap, 0.0f,
                                          start, end, 0, out), -1);
    tf_end();
}
