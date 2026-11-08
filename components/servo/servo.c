/*
 * 舵机模块实现。
 *
 * 职责：
 *   把角度值按线性关系换算成 PWM 脉宽，并通过底层回调输出，
 *   同时提供直接设置脉宽的旁路接口。
 *
 * 数据流：
 *   init() 记录脉宽范围与角度范围 -> set_angle() 先把角度钳位到
 *   [min_angle, max_angle] -> 线性插值得到脉宽 -> ops->set_pulse()。
 *
 * 依赖与边界：
 *   - 无静态状态，可多实例并行；
 *   - 初始化要求 max_pulse_us > min_pulse_us、max_angle > min_angle，
 *     否则后续换算会出现无符号下溢或除零（函数本身不做校验）；
 *   - set_pulse() 不做钳位，调用者需保证脉宽在舵机允许范围内。
 */
#include "servo.h"
#include "log.h"

/*
 * 初始化舵机对象。
 * 参数：servo 对象指针（非 NULL）；ops 操作表（set_pulse 必须实现）；
 *       min_us/max_us 最小/最大脉宽（微秒）；
 *       min_angle/max_angle 最小/最大角度（度）。
 * 返回：0 成功；参数非法返回 -1。
 * 说明：当前角度初始化为 min_angle（仅记录，不立即输出）。
 */
int servo_init(servo_t *servo, const servo_ops_t *ops,
                   uint16_t min_us, uint16_t max_us,
                   uint16_t min_angle, uint16_t max_angle)
{
    if (servo == NULL || ops == NULL || ops->set_pulse == NULL) {
        return -1;
    }
    servo->ops = ops;
    servo->min_pulse_us = min_us;
    servo->max_pulse_us = max_us;
    servo->min_angle = min_angle;
    servo->max_angle = max_angle;
    servo->angle = min_angle;
    return 0;
}

/*
 * 设置舵机角度。
 * 参数：servo 对象指针；angle 目标角度（度，超出范围自动钳位）。
 * 返回：底层 set_pulse 的返回值；servo 为 NULL 返回 -1。
 * 说明：脉宽 = min_pulse + (angle - min_angle) * (max_pulse - min_pulse)
 *       / (max_angle - min_angle)，采用整数运算，结果向下取整。
 */
int servo_set_angle(servo_t *servo, uint16_t angle)
{
    uint32_t range, us;

    if (servo == NULL) {
        return -1;
    }
    /* 角度钳位到机械行程范围内 */
    if (angle < servo->min_angle) {
        angle = servo->min_angle;
    }
    if (angle > servo->max_angle) {
        angle = servo->max_angle;
    }
    /* 角度线性映射到脉宽（要求 max_angle > min_angle） */
    range = (servo->max_pulse_us - servo->min_pulse_us);
    us = servo->min_pulse_us +
         (uint32_t)(angle - servo->min_angle) * range /
         (servo->max_angle - servo->min_angle);

    servo->angle = angle;
    LOG_I("servo", "angle %u", (unsigned)angle);
    return servo->ops->set_pulse(servo->ops->ctx, (uint16_t)us);
}

/*
 * 直接设置 PWM 脉宽。
 * 参数：servo 对象指针；us 脉宽（微秒）。
 * 返回：底层 set_pulse 的返回值；servo 为 NULL 返回 -1。
 * 说明：跳过角度换算，也不更新 servo->angle，适用于校准场景。
 */
int servo_set_pulse(servo_t *servo, uint16_t us)
{
    if (servo == NULL) {
        return -1;
    }
    return servo->ops->set_pulse(servo->ops->ctx, us);
}