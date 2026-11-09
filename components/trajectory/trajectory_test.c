/*
 * trajectory_test.c - 轨迹规划测试（对应 trajectory.c）
 *
 * 覆盖：梯形曲线（满速/短行程降速）、S 曲线（端点、单调性、速度上界、
 *       短行程）、多轴直线插补。
 */

#include "test.h"
#include "trajectory.h"

void ut_trajectory(void)
{
    trajectory_trap_t trap;
    trajectory_scurve_t sc;
    float pos;
    float vel;
    float acc;
    int i;

    tf_begin("trajectory/梯形规划");

    TF_CHECK_INT(trajectory_trap_plan(NULL, 10.0f, 2.0f, 1.0f, 1.0f), -1);
    TF_CHECK_INT(trajectory_trap_plan(&trap, 0.0f, 2.0f, 1.0f, 1.0f), -1);
    TF_CHECK_INT(trajectory_trap_plan(&trap, 10.0f, 0.0f, 1.0f, 1.0f), -1);
    TF_CHECK_INT(trajectory_trap_plan(&trap, 10.0f, 2.0f, 1.0f, 1.0f), 0);

    TF_CHECK_NEAR(trap.vpeak, 2.0f, 1e-5);
    TF_CHECK_NEAR(trap.t_acc, 2.0f, 1e-5);
    TF_CHECK_NEAR(trap.t_cruise, 3.0f, 1e-5);
    TF_CHECK_NEAR(trap.t_total, 7.0f, 1e-5);

    tf_end();

    tf_begin("trajectory/梯形采样");

    TF_CHECK_INT(trajectory_trap_sample(&trap, 0.0f, &pos, &vel, &acc), 0);
    TF_CHECK_NEAR(pos, 0.0f, 1e-6);
    TF_CHECK_NEAR(vel, 0.0f, 1e-6);
    TF_CHECK_NEAR(acc, 1.0f, 1e-6);

    TF_CHECK_INT(trajectory_trap_sample(&trap, 3.0f, &pos, &vel, &acc), 0);
    TF_CHECK_NEAR(pos, 4.0f, 1e-5);
    TF_CHECK_NEAR(vel, 2.0f, 1e-5);
    TF_CHECK_NEAR(acc, 0.0f, 1e-6);

    TF_CHECK_INT(trajectory_trap_sample(&trap, 7.0f, &pos, &vel, &acc), 0);
    TF_CHECK_NEAR(pos, 10.0f, 1e-5);
    TF_CHECK_NEAR(vel, 0.0f, 1e-5);

    /* 超出总时长按端点处理 */
    TF_CHECK_INT(trajectory_trap_sample(&trap, 100.0f, &pos, NULL, NULL), 0);
    TF_CHECK_NEAR(pos, 10.0f, 1e-5);

    /* 全程单调递增且不超过峰值速度 */
    {
        float last = -1.0f;
        for (i = 0; i <= 100; i++) {
            float t = trap.t_total * (float)i / 100.0f;
            trajectory_trap_sample(&trap, t, &pos, &vel, NULL);
            TF_CHECK(pos >= last - 1e-4f);
            TF_CHECK(vel <= trap.vpeak + 1e-4f);
            last = pos;
        }
    }

    TF_CHECK_INT(trajectory_trap_sample(NULL, 1.0f, &pos, NULL, NULL), -1);

    tf_end();

    tf_begin("trajectory/梯形短行程降速");

    TF_CHECK_INT(trajectory_trap_plan(&trap, 0.5f, 2.0f, 1.0f, 1.0f), 0);
    TF_CHECK_NEAR(trap.vpeak, 0.7071068f, 1e-4);
    TF_CHECK_NEAR(trap.t_cruise, 0.0f, 1e-6);
    TF_CHECK_NEAR(trap.t_total, 1.4142136f, 1e-4);
    trajectory_trap_sample(&trap, trap.t_total, &pos, NULL, NULL);
    TF_CHECK_NEAR(pos, 0.5f, 1e-5);

    tf_end();

    tf_begin("trajectory/S 曲线");

    TF_CHECK_INT(trajectory_scurve_plan(NULL, 10.0f, 2.0f, 4.0f, 20.0f), -1);
    TF_CHECK_INT(trajectory_scurve_plan(&sc, 10.0f, 0.0f, 4.0f, 20.0f), -1);
    TF_CHECK_INT(trajectory_scurve_plan(&sc, 10.0f, 2.0f, 4.0f, 20.0f), 0);

    TF_CHECK(sc.t_total > 0.0f);
    TF_CHECK_NEAR(sc.vpeak, 2.0f, 1e-4);

    trajectory_scurve_sample(&sc, 0.0f, &pos, &vel, &acc);
    TF_CHECK_NEAR(pos, 0.0f, 1e-6);
    TF_CHECK_NEAR(vel, 0.0f, 1e-6);
    TF_CHECK_NEAR(acc, 0.0f, 1e-6);

    trajectory_scurve_sample(&sc, sc.t_total, &pos, &vel, NULL);
    TF_CHECK_NEAR(pos, 10.0f, 1e-4);
    TF_CHECK_NEAR(vel, 0.0f, 1e-4);

    {
        float last = -1.0f;
        for (i = 0; i <= 200; i++) {
            float t = sc.t_total * (float)i / 200.0f;
            trajectory_scurve_sample(&sc, t, &pos, &vel, &acc);
            TF_CHECK(pos >= last - 1e-4f);
            TF_CHECK(vel <= sc.vpeak + 1e-3f);
            TF_CHECK(vel >= -1e-3f);
            last = pos;
        }
    }

    TF_CHECK_INT(trajectory_scurve_sample(NULL, 1.0f, &pos, NULL, NULL), -1);

    tf_end();

    tf_begin("trajectory/S 曲线短行程");

    TF_CHECK_INT(trajectory_scurve_plan(&sc, 0.2f, 2.0f, 4.0f, 20.0f), 0);
    TF_CHECK(sc.vpeak < 2.0f);
    TF_CHECK_NEAR(sc.t_cruise, 0.0f, 1e-6);
    trajectory_scurve_sample(&sc, sc.t_total, &pos, &vel, NULL);
    TF_CHECK_NEAR(pos, 0.2f, 1e-4);
    TF_CHECK_NEAR(vel, 0.0f, 1e-4);

    tf_end();

    tf_begin("trajectory/多轴直线插补");

    {
        float start[3] = { 0.0f, 0.0f, 1.0f };
        float end[3] = { 2.0f, 4.0f, -1.0f };
        float out[3];

        TF_CHECK_INT(trajectory_linear_plan(&trap, 1.0f, 1.0f, 1.0f), 0);

        TF_CHECK_INT(trajectory_linear_sample(&trap, 0.0f, start, end, 3U,
                                              out), 0);
        TF_CHECK_NEAR(out[0], 0.0f, 1e-6);
        TF_CHECK_NEAR(out[2], 1.0f, 1e-6);

        TF_CHECK_INT(trajectory_linear_sample(&trap, trap.t_total, start, end,
                                              3U, out), 0);
        TF_CHECK_NEAR(out[0], 2.0f, 1e-5);
        TF_CHECK_NEAR(out[1], 4.0f, 1e-5);
        TF_CHECK_NEAR(out[2], -1.0f, 1e-5);

        TF_CHECK_INT(trajectory_linear_sample(&trap, 0.0f, start, end, 0U,
                                              out), -1);
        TF_CHECK_INT(trajectory_linear_sample(NULL, 0.0f, start, end, 3U,
                                              out), -1);
        TF_CHECK_INT(trajectory_linear_sample(&trap, 0.0f, NULL, end, 3U,
                                              out), -1);
    }

    tf_end();
}
