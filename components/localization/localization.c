/*
 * localization.c - 二维 EKF 定位实现
 *
 * 运动模型（差速底盘）：
 *   x' = x + v*dt*cos(theta)
 *   y' = y + v*dt*sin(theta)
 *   th' = theta + w*dt
 * 预测：P = F*P*F^T + Q
 * 观测：H = I（绝对位姿）或 H = [0 0 1]（仅偏航）
 */

#include "localization.h"

#include <stddef.h>

#include <math.h>

#define LOC_PI 3.14159265358979323846f

static float wrap_pi(float a)
{
    while (a > LOC_PI) {
        a -= 2.0f * LOC_PI;
    }
    while (a < -LOC_PI) {
        a += 2.0f * LOC_PI;
    }
    return a;
}

/* C = A * B（3x3） */
static void mat3_mul(const float A[3][3], const float B[3][3], float C[3][3])
{
    int i;
    int j;
    int k;
    float tmp[3][3];

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            float sum = 0.0f;
            for (k = 0; k < 3; k++) {
                sum += A[i][k] * B[k][j];
            }
            tmp[i][j] = sum;
        }
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            C[i][j] = tmp[i][j];
        }
    }
}

/* 求 3x3 逆矩阵；成功返回 0，奇异返回 -1 */
static int mat3_inv(const float A[3][3], float out[3][3])
{
    float c00 = A[1][1] * A[2][2] - A[1][2] * A[2][1];
    float c01 = A[1][2] * A[2][0] - A[1][0] * A[2][2];
    float c02 = A[1][0] * A[2][1] - A[1][1] * A[2][0];
    float det = A[0][0] * c00 + A[0][1] * c01 + A[0][2] * c02;
    float inv_det;

    if (fabsf(det) < 1e-12f) {
        return -1;
    }
    inv_det = 1.0f / det;

    out[0][0] = c00 * inv_det;
    out[0][1] = (A[0][2] * A[2][1] - A[0][1] * A[2][2]) * inv_det;
    out[0][2] = (A[0][1] * A[1][2] - A[0][2] * A[1][1]) * inv_det;

    out[1][0] = c01 * inv_det;
    out[1][1] = (A[0][0] * A[2][2] - A[0][2] * A[2][0]) * inv_det;
    out[1][2] = (A[0][2] * A[1][0] - A[0][0] * A[1][2]) * inv_det;

    out[2][0] = c02 * inv_det;
    out[2][1] = (A[0][1] * A[2][0] - A[0][0] * A[2][1]) * inv_det;
    out[2][2] = (A[0][0] * A[1][1] - A[0][1] * A[1][0]) * inv_det;
    return 0;
}

int loc_ekf_init(localization_ekf_t *ekf, float x, float y, float theta,
                 float q_pos, float q_yaw, float r_pos, float r_yaw)
{
    int i;
    int j;

    if (ekf == NULL || !(q_pos > 0.0f) || !(q_yaw > 0.0f) ||
        !(r_pos > 0.0f) || !(r_yaw > 0.0f)) {
        return -1;
    }

    ekf->state.x = x;
    ekf->state.y = y;
    ekf->state.theta = wrap_pi(theta);

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            ekf->P[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }

    ekf->q_pos = q_pos;
    ekf->q_yaw = q_yaw;
    ekf->r_pos = r_pos;
    ekf->r_yaw = r_yaw;
    ekf->initialized = true;
    return 0;
}

int loc_ekf_reset(localization_ekf_t *ekf, float x, float y, float theta)
{
    int i;
    int j;

    if (ekf == NULL || !ekf->initialized) {
        return -1;
    }
    ekf->state.x = x;
    ekf->state.y = y;
    ekf->state.theta = wrap_pi(theta);
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            ekf->P[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
    return 0;
}

int loc_ekf_predict(localization_ekf_t *ekf, float v, float w, float dt)
{
    float c;
    float s;
    float F[3][3];
    float FT[3][3];
    float tmp[3][3];
    float Pnew[3][3];
    int i;
    int j;

    if (ekf == NULL || !ekf->initialized || !(dt > 0.0f)) {
        return -1;
    }

    c = cosf(ekf->state.theta);
    s = sinf(ekf->state.theta);

    ekf->state.x += v * dt * c;
    ekf->state.y += v * dt * s;
    ekf->state.theta = wrap_pi(ekf->state.theta + w * dt);

    F[0][0] = 1.0f; F[0][1] = 0.0f; F[0][2] = -v * dt * s;
    F[1][0] = 0.0f; F[1][1] = 1.0f; F[1][2] =  v * dt * c;
    F[2][0] = 0.0f; F[2][1] = 0.0f; F[2][2] =  1.0f;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            FT[i][j] = F[j][i];
        }
    }

    mat3_mul(F, ekf->P, tmp);
    mat3_mul(tmp, FT, Pnew);

    Pnew[0][0] += ekf->q_pos;
    Pnew[1][1] += ekf->q_pos;
    Pnew[2][2] += ekf->q_yaw;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            ekf->P[i][j] = Pnew[i][j];
        }
    }
    return 0;
}

int loc_ekf_update_pose(localization_ekf_t *ekf, const pose2d_t *measurement)
{
    float innov[3];
    float S[3][3];
    float Sinv[3][3];
    float K[3][3];
    float IKH[3][3];
    int i;
    int j;

    if (ekf == NULL || !ekf->initialized || measurement == NULL) {
        return -1;
    }

    innov[0] = measurement->x - ekf->state.x;
    innov[1] = measurement->y - ekf->state.y;
    innov[2] = wrap_pi(measurement->theta - ekf->state.theta);

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            S[i][j] = ekf->P[i][j];
        }
    }
    S[0][0] += ekf->r_pos;
    S[1][1] += ekf->r_pos;
    S[2][2] += ekf->r_yaw;

    if (mat3_inv(S, Sinv) != 0) {
        return -1;
    }
    mat3_mul(ekf->P, Sinv, K);

    ekf->state.x += K[0][0] * innov[0] + K[0][1] * innov[1] + K[0][2] * innov[2];
    ekf->state.y += K[1][0] * innov[0] + K[1][1] * innov[1] + K[1][2] * innov[2];
    ekf->state.theta = wrap_pi(ekf->state.theta +
                               K[2][0] * innov[0] + K[2][1] * innov[1] +
                               K[2][2] * innov[2]);

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            IKH[i][j] = ((i == j) ? 1.0f : 0.0f) - K[i][j];
        }
    }
    {
        float Pnew[3][3];
        mat3_mul(IKH, ekf->P, Pnew);
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++) {
                ekf->P[i][j] = Pnew[i][j];
            }
        }
    }
    return 0;
}

int loc_ekf_update_yaw(localization_ekf_t *ekf, float yaw)
{
    float innov;
    float PHt[3];
    float s;
    float K[3];
    int i;

    if (ekf == NULL || !ekf->initialized) {
        return -1;
    }

    innov = wrap_pi(yaw - ekf->state.theta);
    for (i = 0; i < 3; i++) {
        PHt[i] = ekf->P[i][2];
    }
    s = ekf->P[2][2] + ekf->r_yaw;
    if (!(s > 1e-12f)) {
        return -1;
    }

    for (i = 0; i < 3; i++) {
        K[i] = PHt[i] / s;
    }

    ekf->state.x += K[0] * innov;
    ekf->state.y += K[1] * innov;
    ekf->state.theta = wrap_pi(ekf->state.theta + K[2] * innov);

    for (i = 0; i < 3; i++) {
        int j;
        for (j = 0; j < 3; j++) {
            ekf->P[i][j] -= K[i] * PHt[j];
        }
    }
    return 0;
}

const pose2d_t *loc_ekf_pose(const localization_ekf_t *ekf)
{
    return (ekf != NULL) ? &ekf->state : NULL;
}
