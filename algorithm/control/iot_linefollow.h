#ifndef IOT_LINEFOLLOW_H
#define IOT_LINEFOLLOW_H

/*
 * 机器人寻迹算法（灰度传感器阵列）：
 *  1) 加权平均法计算线相对位置（-1..+1）
 *  2) PID 纠偏输出左右轮差速
 */

#include "iot_types.h"
#include "iot_pid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 计算线位置：values 为各传感器读数（线越强读数越大），threshold 用于二值化。
 * position 归一化到 -1..+1（0 表示居中，负=偏左，正=偏右） */
int iot_line_position(const int16_t *values, uint8_t count,
                      int16_t threshold, float *position);

typedef struct {
    uint8_t num_sensors;
    int16_t threshold;

    iot_pid_t pid;

    float position;     /* 当前 -1..+1 */
    float base_speed;   /* 基准速度 */
    float max_speed;
    float left_speed;   /* 左轮输出 */
    float right_speed;  /* 右轮输出 */
} iot_line_follower_t;

int iot_line_follow_init(iot_line_follower_t *f, float base_speed, float max_speed,
                         float kp, float ki, float kd, int16_t threshold,
                         uint8_t num_sensors);

/* 每拍更新：计算位置 -> PID 纠偏 -> 输出左右轮速 */
int iot_line_follow_update(iot_line_follower_t *f, const int16_t *values, float dt);

#ifdef __cplusplus
}
#endif

#endif /* IOT_LINEFOLLOW_H */