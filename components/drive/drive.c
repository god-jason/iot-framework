/*
 * drive.c - 闭环伺服驱动实现
 *
 * 控制结构：
 *   位置误差 --PID--> 速度指令（限幅） --PID--> 输出（限幅）
 * 若关闭速度环，则位置 PID 直接输出执行量。
 * 速度由位置差分得到；首次更新只建立基准。
 */

#include "drive.h"

#include <stddef.h>

static float clampf_drive(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int drive_init(drive_t *drive, const drive_ops_t *ops,
               float kp_pos, float ki_pos, float kd_pos,
               float kp_vel, float ki_vel, float kd_vel)
{
    if (drive == NULL || ops == NULL ||
        ops->read_position == NULL || ops->write_output == NULL) {
        return -1;
    }

    drive->ops = ops;
    pid_init(&drive->pos_pid, kp_pos, ki_pos, kd_pos);
    pid_init(&drive->vel_pid, kp_vel, ki_vel, kd_vel);

    /* 速度环默认启用；位置环输出即速度指令，限幅在 drive_set_limits 设置 */
    drive->use_velocity_loop = true;

    drive->target = 0.0f;
    drive->position = 0.0f;
    drive->velocity = 0.0f;
    drive->last_position = 0.0f;

    drive->vel_limit = 10.0f;
    drive->out_min = -1.0f;
    drive->out_max = 1.0f;
    drive->output = 0.0f;

    pid_set_limits(&drive->pos_pid, -drive->vel_limit, drive->vel_limit);
    pid_set_limits(&drive->vel_pid, drive->out_min, drive->out_max);

    drive->primed = false;
    return 0;
}

int drive_set_target(drive_t *drive, float target)
{
    if (drive == NULL) {
        return -1;
    }
    drive->target = target;
    return 0;
}

int drive_set_limits(drive_t *drive, float vel_limit,
                     float out_min, float out_max)
{
    if (drive == NULL || !(vel_limit > 0.0f) || !(out_min < out_max)) {
        return -1;
    }
    drive->vel_limit = vel_limit;
    drive->out_min = out_min;
    drive->out_max = out_max;

    pid_set_limits(&drive->pos_pid, -vel_limit, vel_limit);
    pid_set_limits(&drive->vel_pid, out_min, out_max);
    return 0;
}

int drive_reset(drive_t *drive, float position)
{
    if (drive == NULL) {
        return -1;
    }
    pid_reset(&drive->pos_pid);
    pid_reset(&drive->vel_pid);
    drive->position = position;
    drive->last_position = position;
    drive->velocity = 0.0f;
    drive->output = 0.0f;
    drive->primed = true;
    return 0;
}

int drive_position(const drive_t *drive, float *pos)
{
    if (drive == NULL || pos == NULL) {
        return -1;
    }
    *pos = drive->position;
    return 0;
}

float drive_update(drive_t *drive, float dt)
{
    float pos = 0.0f;
    float vel_cmd;
    float out;

    if (drive == NULL || !(dt > 0.0f)) {
        return (drive != NULL) ? drive->output : 0.0f;
    }
    if (drive->ops->read_position(drive->ops->ctx, &pos) != 0) {
        return drive->output;
    }

    drive->position = pos;
    if (!drive->primed) {
        drive->last_position = pos;
        drive->velocity = 0.0f;
        drive->primed = true;
    } else {
        drive->velocity = (pos - drive->last_position) / dt;
        drive->last_position = pos;
    }

    if (drive->use_velocity_loop) {
        vel_cmd = pid_update(&drive->pos_pid, drive->target, pos, dt);
        vel_cmd = clampf_drive(vel_cmd, -drive->vel_limit, drive->vel_limit);
        out = pid_update(&drive->vel_pid, vel_cmd, drive->velocity, dt);
    } else {
        out = pid_update(&drive->pos_pid, drive->target, pos, dt);
    }

    out = clampf_drive(out, drive->out_min, drive->out_max);
    drive->output = out;
    (void)drive->ops->write_output(drive->ops->ctx, out);
    return out;
}
