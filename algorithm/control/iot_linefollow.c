#include "iot_linefollow.h"
#include "iot_math.h"

int iot_line_position(const int16_t *values, uint8_t count,
                      int16_t threshold, float *position)
{
    float num = 0.0f, den = 0.0f;
    uint8_t i;
    float w;

    if (values == NULL || position == NULL || count == 0) {
        return IOT_ERR_PARAM;
    }

    /* 权重 = 大于阈值的读数（线越强权重越大） */
    for (i = 0; i < count; i++) {
        if (values[i] > threshold) {
            w = (float)(values[i] - threshold);
        } else {
            w = 0.0f;
        }
        num += w * (float)i;
        den += w;
    }

    if (den <= 0.0f) {
        *position = 0.0f;   /* 未检测到线：保持原位 */
        return IOT_OK;
    }
    /* 归一化到 -1..+1 */
    *position = (num / den - (float)(count - 1) / 2.0f) /
                ((float)(count - 1) / 2.0f);
    return IOT_OK;
}

int iot_line_follow_init(iot_line_follower_t *f, float base_speed, float max_speed,
                         float kp, float ki, float kd, int16_t threshold,
                         uint8_t num_sensors)
{
    if (f == NULL || num_sensors == 0) {
        return IOT_ERR_PARAM;
    }
    f->num_sensors = num_sensors;
    f->threshold = threshold;
    f->base_speed = base_speed;
    f->max_speed = max_speed;
    f->position = 0.0f;
    f->left_speed = base_speed;
    f->right_speed = base_speed;
    iot_pid_init(&f->pid, kp, ki, kd);
    return IOT_OK;
}

int iot_line_follow_update(iot_line_follower_t *f, const int16_t *values, float dt)
{
    float correction;

    if (f == NULL || values == NULL) {
        return IOT_ERR_PARAM;
    }
    iot_line_position(values, f->num_sensors, f->threshold, &f->position);

    /* PID 以「位置归零」为目标，输出纠偏量 */
    correction = iot_pid_update(&f->pid, 0.0f, f->position, dt);

    /* 差速：左轮 +=（左偏时向右转，即左轮加速），符号视布置而定 */
    f->left_speed = f->base_speed + correction;
    f->right_speed = f->base_speed - correction;

    f->left_speed = iot_math_clamp(f->left_speed, -f->max_speed, f->max_speed);
    f->right_speed = iot_math_clamp(f->right_speed, -f->max_speed, f->max_speed);
    return IOT_OK;
}