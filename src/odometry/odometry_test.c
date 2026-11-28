/*
 * odometry_test.c - 里程计测试（对应 odometry.c）
 *
 * 覆盖：直线、原地旋转、圆弧精确积分、脉冲输入与基准建立、
 *       角度归一化、非法参数。
 */

#include "test.h"
#include "odometry.h"

void ut_odometry(void)
{
    diff_drive_t diff;
    odometry_t odom;
    pose2d_t pose;

    diff_drive_init(&diff, 0.05f, 1.0f, 1000.0f);

    tf_begin("odometry/初始化与直线");

    TF_CHECK_INT(odometry_init(NULL, &diff, 0.0f, 0.0f, 0.0f), -1);
    TF_CHECK_INT(odometry_init(&odom, NULL, 0.0f, 0.0f, 0.0f), -1);
    TF_CHECK_INT(odometry_init(&odom, &diff, 0.0f, 0.0f, 0.0f), 0);

    TF_CHECK_INT(odometry_update_distance(&odom, 1.0f, 1.0f, &pose), 0);
    TF_CHECK_NEAR(pose.x, 1.0f, 1e-5);
    TF_CHECK_NEAR(pose.y, 0.0f, 1e-5);
    TF_CHECK_NEAR(pose.theta, 0.0f, 1e-5);

    tf_end();

    tf_begin("odometry/原地旋转与圆弧");

    odometry_reset(&odom, 0.0f, 0.0f, 0.0f);
    TF_CHECK_INT(odometry_update_distance(&odom, -0.5f, 0.5f, &pose), 0);
    TF_CHECK_NEAR(pose.x, 0.0f, 1e-5);
    TF_CHECK_NEAR(pose.y, 0.0f, 1e-5);
    TF_CHECK_NEAR(pose.theta, 1.0f, 1e-5);

    odometry_reset(&odom, 0.0f, 0.0f, 0.0f);
    /* dl=0.9, dr=1.1, base=1 -> ds=1, dth=0.2, R=5 */
    TF_CHECK_INT(odometry_update_distance(&odom, 0.9f, 1.1f, &pose), 0);
    TF_CHECK_NEAR(pose.x, 5.0f * 0.198669f, 1e-4);
    TF_CHECK_NEAR(pose.y, 5.0f * 0.019933f, 1e-4);
    TF_CHECK_NEAR(pose.theta, 0.2f, 1e-5);

    tf_end();

    tf_begin("odometry/编码器脉冲输入");

    odometry_reset(&odom, 0.0f, 0.0f, 0.0f);
    /* 首次只建立基准 */
    TF_CHECK_INT(odometry_update(&odom, 100, 100, &pose), 0);
    TF_CHECK_NEAR(pose.x, 0.0f, 1e-6);

    /* 左右各增加 1000 脉冲 = 一圈 */
    TF_CHECK_INT(odometry_update(&odom, 1100, 1100, &pose), 0);
    TF_CHECK_NEAR(pose.x, 0.3141593f, 1e-4);

    /* 再次重置后需要重新建立基准 */
    odometry_reset(&odom, 1.0f, 2.0f, 0.0f);
    TF_CHECK_INT(odometry_update(&odom, 50, 50, &pose), 0);
    TF_CHECK_NEAR(pose.x, 1.0f, 1e-6);
    TF_CHECK_NEAR(pose.y, 2.0f, 1e-6);

    tf_end();

    tf_begin("odometry/角度归一化与非法参数");

    odometry_reset(&odom, 0.0f, 0.0f, 3.0f);
    TF_CHECK_INT(odometry_update_distance(&odom, -1.0f, 1.0f, &pose), 0);
    TF_CHECK(pose.theta >= -3.1416f && pose.theta <= 3.1416f);

    TF_CHECK_INT(odometry_update_distance(NULL, 0.0f, 0.0f, &pose), -1);
    TF_CHECK_INT(odometry_update(NULL, 0, 0, &pose), -1);
    TF_CHECK_INT(odometry_reset(NULL, 0.0f, 0.0f, 0.0f), -1);

    {
        diff_drive_t bad;
        memset(&bad, 0, sizeof(bad));
        TF_CHECK_INT(odometry_init(&odom, &bad, 0.0f, 0.0f, 0.0f), -1);
    }

    tf_end();
}
