#ifndef IMU_H
#define IMU_H

/*
 * imu.h - 六轴姿态融合（Mahony AHRS）
 *
 * 输入陀螺仪（rad/s）与加速度计（任意单位，内部归一化），
 * 输出四元数与欧拉角。加速度计用于校正俯仰/横滚，偏航由陀螺仪积分。
 *
 * 无动态内存；调用者需按固定周期调用 imu_ahrs_update()。
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float q[4];          /* 姿态四元数：w, x, y, z */
    float two_kp;        /* 2 * 比例增益 */
    float two_ki;        /* 2 * 积分增益 */
    float integral[3];   /* 误差积分（陀螺零偏估计） */
    bool initialized;
} imu_ahrs_t;

/*
 * 初始化 AHRS。
 * kp/ki 为 Mahony 增益，典型值 kp=1.0、ki=0.0~0.1。
 */
void imu_ahrs_init(imu_ahrs_t *ahrs, float kp, float ki);

/*
 * 更新一次姿态。
 * gyro  : 陀螺仪角速度（rad/s），长度 3
 * accel : 加速度计读数（任意单位），长度 3
 * dt    : 采样周期（秒，必须 > 0）
 */
void imu_ahrs_update(imu_ahrs_t *ahrs, const float gyro[3],
                     const float accel[3], float dt);

/* 读取欧拉角（弧度）；任一输出可为 NULL */
void imu_ahrs_get_euler(const imu_ahrs_t *ahrs,
                        float *roll, float *pitch, float *yaw);

#ifdef __cplusplus
}
#endif

#endif /* IMU_H */
