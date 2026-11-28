#ifndef LOCALIZATION_H
#define LOCALIZATION_H

/*
 * localization.h - 二维扩展卡尔曼定位（EKF）
 *
 * 状态：[x, y, theta]，运动模型为差速底盘单车模型；
 * 观测支持：
 *   - 绝对位姿（如激光匹配/二维码定位）；
 *   - 单一偏航角（如磁力计/IMU）。
 *
 * 无动态内存；3x3 协方差矩阵内嵌在对象中。
 */

#include <stdbool.h>
#include "kinematics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    pose2d_t state;     /* 当前估计位姿 */

    float P[3][3];      /* 协方差矩阵 */

    float q_pos;        /* 过程噪声：位置（米²） */
    float q_yaw;        /* 过程噪声：朝向（弧度²） */
    float r_pos;        /* 观测噪声：位置（米²） */
    float r_yaw;        /* 观测噪声：朝向（弧度²） */

    bool initialized;
} localization_ekf_t;

/*
 * 初始化 EKF。
 * 返回：0 成功；-1 参数非法或噪声参数不为正。
 */
int loc_ekf_init(localization_ekf_t *ekf, float x, float y, float theta,
                 float q_pos, float q_yaw, float r_pos, float r_yaw);

/* 直接用新位姿重置状态与协方差（例如重定位） */
int loc_ekf_reset(localization_ekf_t *ekf, float x, float y, float theta);

/*
 * 预测步：按车体线速度 v（m/s）与角速度 w（rad/s）推进 dt 秒。
 * 返回：0 成功；-1 参数非法。
 */
int loc_ekf_predict(localization_ekf_t *ekf, float v, float w, float dt);

/*
 * 更新步一：绝对位姿观测（x, y, theta 均有效）。
 * 返回：0 成功；-1 参数非法。
 */
int loc_ekf_update_pose(localization_ekf_t *ekf, const pose2d_t *measurement);

/*
 * 更新步二：仅偏航角观测（例如 IMU/磁力计）。
 * 返回：0 成功；-1 参数非法。
 */
int loc_ekf_update_yaw(localization_ekf_t *ekf, float yaw);

/* 读取当前估计位姿；ekf 为 NULL 时返回 NULL */
const pose2d_t *loc_ekf_pose(const localization_ekf_t *ekf);

#ifdef __cplusplus
}
#endif

#endif /* LOCALIZATION_H */
