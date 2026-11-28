/*
 * imu.c - Mahony 六轴姿态融合实现
 *
 * 步骤：
 *   1. 归一化加速度计读数，估计重力方向；
 *   2. 由四元数计算当前重力方向，与测量值叉乘得到误差；
 *   3. 误差积分（可选）并补偿到角速度；
 *   4. 一阶积分四元数并归一化。
 * 加速度模长过小时跳过校正，只做陀螺积分。
 */

#include "imu.h"

#include <stddef.h>

#include <math.h>

/* 四元数更新步长保护 */
#define IMU_MIN_DT 1e-6f

void imu_ahrs_init(imu_ahrs_t *ahrs, float kp, float ki)
{
    if (ahrs == NULL) {
        return;
    }
    ahrs->q[0] = 1.0f;
    ahrs->q[1] = 0.0f;
    ahrs->q[2] = 0.0f;
    ahrs->q[3] = 0.0f;
    ahrs->two_kp = 2.0f * kp;
    ahrs->two_ki = 2.0f * ki;
    ahrs->integral[0] = 0.0f;
    ahrs->integral[1] = 0.0f;
    ahrs->integral[2] = 0.0f;
    ahrs->initialized = true;
}

void imu_ahrs_update(imu_ahrs_t *ahrs, const float gyro[3],
                     const float accel[3], float dt)
{
    float q0;
    float q1;
    float q2;
    float q3;
    float gx;
    float gy;
    float gz;
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    float norm;
    float vx;
    float vy;
    float vz;
    float ex;
    float ey;
    float ez;

    if (ahrs == NULL || gyro == NULL || !(dt > IMU_MIN_DT)) {
        return;
    }
    if (!ahrs->initialized) {
        imu_ahrs_init(ahrs, ahrs->two_kp * 0.5f, ahrs->two_ki * 0.5f);
    }

    q0 = ahrs->q[0];
    q1 = ahrs->q[1];
    q2 = ahrs->q[2];
    q3 = ahrs->q[3];
    gx = gyro[0];
    gy = gyro[1];
    gz = gyro[2];

    ex = 0.0f;
    ey = 0.0f;
    ez = 0.0f;

    if (accel != NULL) {
        ax = accel[0];
        ay = accel[1];
        az = accel[2];
        norm = sqrtf(ax * ax + ay * ay + az * az);
        if (norm > 1e-3f) {
            ax /= norm;
            ay /= norm;
            az /= norm;

            /* 由四元数估计的重力方向（机体系） */
            vx = 2.0f * (q1 * q3 - q0 * q2);
            vy = 2.0f * (q0 * q1 + q2 * q3);
            vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

            /* 测量重力与估计重力叉乘 = 姿态误差 */
            ex = ay * vz - az * vy;
            ey = az * vx - ax * vz;
            ez = ax * vy - ay * vx;

            if (ahrs->two_ki > 0.0f) {
                ahrs->integral[0] += ahrs->two_ki * ex * dt;
                ahrs->integral[1] += ahrs->two_ki * ey * dt;
                ahrs->integral[2] += ahrs->two_ki * ez * dt;
                gx += ahrs->integral[0];
                gy += ahrs->integral[1];
                gz += ahrs->integral[2];
            }

            gx += ahrs->two_kp * ex;
            gy += ahrs->two_kp * ey;
            gz += ahrs->two_kp * ez;
        }
    }

    /* 四元数一阶积分：q' = 0.5 * q ⊗ (0, g) */
    {
        float half_dt = 0.5f * dt;
        float dq0 = half_dt * (-q1 * gx - q2 * gy - q3 * gz);
        float dq1 = half_dt * (q0 * gx + q2 * gz - q3 * gy);
        float dq2 = half_dt * (q0 * gy - q1 * gz + q3 * gx);
        float dq3 = half_dt * (q0 * gz + q1 * gy - q2 * gx);

        q0 += dq0;
        q1 += dq1;
        q2 += dq2;
        q3 += dq3;
    }

    norm = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    if (norm < 1e-6f) {
        q0 = 1.0f;
        q1 = 0.0f;
        q2 = 0.0f;
        q3 = 0.0f;
    } else {
        q0 /= norm;
        q1 /= norm;
        q2 /= norm;
        q3 /= norm;
    }

    ahrs->q[0] = q0;
    ahrs->q[1] = q1;
    ahrs->q[2] = q2;
    ahrs->q[3] = q3;
}

void imu_ahrs_get_euler(const imu_ahrs_t *ahrs,
                        float *roll, float *pitch, float *yaw)
{
    float q0;
    float q1;
    float q2;
    float q3;

    if (ahrs == NULL) {
        return;
    }

    q0 = ahrs->q[0];
    q1 = ahrs->q[1];
    q2 = ahrs->q[2];
    q3 = ahrs->q[3];

    if (roll != NULL) {
        *roll = atan2f(2.0f * (q0 * q1 + q2 * q3),
                       1.0f - 2.0f * (q1 * q1 + q2 * q2));
    }
    if (pitch != NULL) {
        float sinp = 2.0f * (q0 * q2 - q3 * q1);
        if (sinp > 1.0f) sinp = 1.0f;
        if (sinp < -1.0f) sinp = -1.0f;
        *pitch = asinf(sinp);
    }
    if (yaw != NULL) {
        *yaw = atan2f(2.0f * (q0 * q3 + q1 * q2),
                      1.0f - 2.0f * (q2 * q2 + q3 * q3));
    }
}
