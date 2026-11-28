#ifndef KINEMATICS_H
#define KINEMATICS_H

/*
 * kinematics.h - 机器人运动学（纯算法，无硬件依赖）
 *
 * 提供三类模型：
 *   - 差速轮：线速度/角速度 <-> 左右轮线速度，脉冲与位移换算；
 *   - 阿克曼：后轮驱动 + 前轮转向，速度与转角分配；
 *   - 两连杆平面机械臂：正解与逆解。
 *
 * 单位约定：长度米、角度弧度、时间秒、速度 m/s。
 * 所有接口无动态内存、无全局状态，可重入。
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================== 通用位姿 ============================== */

/* 平面位姿：世界坐标（米）与朝向角（弧度，逆时针为正） */
typedef struct {
    float x;
    float y;
    float theta;
} pose2d_t;

/* ============================== 差速轮 ============================== */

/* 两轮独立驱动的差速底盘参数 */
typedef struct {
    float wheel_radius;   /* 轮半径（米），必须 > 0 */
    float wheel_base;     /* 左右轮间距（米），必须 > 0 */
    float ticks_per_rev;  /* 每转脉冲数（含减速比），必须 > 0 */
} diff_drive_t;

/*
 * 初始化差速底盘参数。
 * 返回：0 成功；-1 任一参数非法。
 */
int diff_drive_init(diff_drive_t *drive, float wheel_radius,
                    float wheel_base, float ticks_per_rev);

/*
 * 正解：车体线速度/角速度 -> 左右轮线速度（m/s）。
 * w > 0 表示逆时针（左转），此时右轮更快。
 * 返回：0 成功；-1 参数非法。
 */
int diff_drive_unicycle(const diff_drive_t *drive, float v, float w,
                        float *v_left, float *v_right);

/*
 * 逆解：左右轮线速度 -> 车体线速度/角速度。
 * 返回：0 成功；-1 参数非法。
 */
int diff_drive_inverse(const diff_drive_t *drive, float v_left, float v_right,
                       float *v, float *w);

/* 编码器脉冲 -> 轮面位移（米） */
float diff_drive_ticks_to_meters(const diff_drive_t *drive, int32_t ticks);

/* 轮面位移（米）-> 编码器脉冲（四舍五入） */
int32_t diff_drive_meters_to_ticks(const diff_drive_t *drive, float meters);

/* ============================== 阿克曼 ============================== */

/* 阿克曼底盘：后轮驱动、前轮转向 */
typedef struct {
    float wheelbase;   /* 前后轴距（米），必须 > 0 */
    float track;       /* 轮距（米），必须 > 0 */
} ackermann_t;

/*
 * 初始化阿克曼参数。
 * 返回：0 成功；-1 参数非法。
 */
int ackermann_init(ackermann_t *ack, float wheelbase, float track);

/*
 * 由车体速度 v 与前轮转角 steer（弧度）计算左右后轮速度与左右前轮转角。
 * steer > 0 为左转；转角过大（转向半径小于半个轮距）时报错。
 * 返回：0 成功；-1 参数非法或转向几何不可实现。
 */
int ackermann_unicycle(const ackermann_t *ack, float v, float steer,
                       float *v_rear_left, float *v_rear_right,
                       float *steer_left, float *steer_right);

/* ============================== 两连杆机械臂 ============================== */

/*
 * 两连杆平面机械臂正解（关节角 -> 末端坐标）。
 * q1 为第一连杆绝对角，q2 为第二连杆相对角。
 * 返回：0 成功；-1 参数非法。
 */
int arm2_fk(float l1, float l2, float q1, float q2, float *x, float *y);

/*
 * 两连杆平面机械臂逆解（末端坐标 -> 关节角）。
 * elbow_up 为 true 时选择 q2 >= 0 的解。
 * 返回：0 成功；-1 参数非法或目标不可达。
 */
int arm2_ik(float l1, float l2, float x, float y, bool elbow_up,
            float *q1, float *q2);

#ifdef __cplusplus
}
#endif

#endif /* KINEMATICS_H */
