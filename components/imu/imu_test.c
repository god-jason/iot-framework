/*
 * imu_test.c - 姿态融合测试（对应 imu.c）
 *
 * 覆盖：静止对准、偏航陀螺积分、零加速度降级、NULL 与非法 dt。
 */

#include "test.h"
#include "imu.h"

void ut_imu(void)
{
    imu_ahrs_t ahrs;
    float gyro[3];
    float accel[3];
    float roll;
    float pitch;
    float yaw;
    int i;

    tf_begin("imu/静止对准");

    imu_ahrs_init(&ahrs, 1.0f, 0.0f);
    TF_CHECK(ahrs.initialized);
    TF_CHECK_NEAR(ahrs.q[0], 1.0f, 1e-6);
    TF_CHECK_NEAR(ahrs.q[1], 0.0f, 1e-6);

    gyro[0] = 0.0f; gyro[1] = 0.0f; gyro[2] = 0.0f;
    accel[0] = 0.0f; accel[1] = 0.0f; accel[2] = 1.0f;
    for (i = 0; i < 500; i++) {
        imu_ahrs_update(&ahrs, gyro, accel, 0.01f);
    }
    imu_ahrs_get_euler(&ahrs, &roll, &pitch, &yaw);
    TF_CHECK_NEAR(roll, 0.0f, 1e-3);
    TF_CHECK_NEAR(pitch, 0.0f, 1e-3);
    TF_CHECK_NEAR(yaw, 0.0f, 1e-3);

    tf_end();

    tf_begin("imu/偏航角积分");

    imu_ahrs_init(&ahrs, 1.0f, 0.0f);
    gyro[0] = 0.0f; gyro[1] = 0.0f; gyro[2] = 1.0f;
    for (i = 0; i < 100; i++) {              /* 1 s，约 1 rad */
        imu_ahrs_update(&ahrs, gyro, accel, 0.01f);
    }
    imu_ahrs_get_euler(&ahrs, NULL, NULL, &yaw);
    TF_CHECK_NEAR(yaw, 1.0f, 0.03);

    /* 反向旋转 */
    gyro[2] = -1.0f;
    for (i = 0; i < 100; i++) {
        imu_ahrs_update(&ahrs, gyro, accel, 0.01f);
    }
    imu_ahrs_get_euler(&ahrs, NULL, NULL, &yaw);
    TF_CHECK_NEAR(yaw, 0.0f, 0.05);

    tf_end();

    tf_begin("imu/零加速度与非法输入");

    gyro[0] = 0.0f; gyro[1] = 0.0f; gyro[2] = 0.1f;
    accel[0] = 0.0f; accel[1] = 0.0f; accel[2] = 0.0f;
    imu_ahrs_update(&ahrs, gyro, accel, 0.01f);   /* 无重力参考：只积分 */
    imu_ahrs_update(&ahrs, gyro, NULL, 0.01f);
    imu_ahrs_update(&ahrs, gyro, accel, 0.0f);    /* dt 非法：忽略 */
    imu_ahrs_update(&ahrs, gyro, accel, -1.0f);
    imu_ahrs_update(NULL, gyro, accel, 0.01f);
    imu_ahrs_update(&ahrs, NULL, accel, 0.01f);
    imu_ahrs_get_euler(NULL, &roll, &pitch, &yaw);
    TF_CHECK(1);                                  /* 不崩溃即可 */

    tf_end();
}
