/*
 * 电机抽象模块实现。
 *
 * 职责：
 *   为不同类型的电机驱动提供统一的方向 / 速度控制接口，上层不感知
 *   具体驱动芯片（H 桥、电机驱动模组等）。
 *
 * 数据流：
 *   init() 绑定 ops 并置默认最大速度 1000 -> set_speed() 限幅后调用
 *   底层 set_speed -> set_direction()/stop() 直接透传或兜底降速。
 *
 * 依赖与边界：
 *   - 速度范围 [-max_speed, max_speed]，正负表示方向；
 *   - max_speed 由上层直接修改结构体字段调整，模块不提供 setter；
 *   - stop() 优先使用底层 stop 回调，未实现时退化为设置速度 0；
 *   - 所有接口遇到空指针或缺失回调时返回 -1。
 */
#include "motor.h"

/*
 * 初始化电机对象。
 * 参数：motor 对象指针（非 NULL）；ops 操作表（非 NULL）。
 * 返回：0 成功；参数非法返回 -1。
 * 说明：默认速度 0、最大速度 1000、状态为停止；
 *       不会主动调用底层初始化（驱动需自行完成）。
 */
int motor_init(motor_t *motor, const motor_ops_t *ops)
{
    if (motor == NULL || ops == NULL) {
        return -1;
    }
    motor->ops = ops;
    motor->speed = 0;
    motor->max_speed = 1000;
    motor->direction = MOTOR_STOPPED;
    return 0;
}

/*
 * 设置电机速度。
 * 参数：motor 对象指针；speed 目标速度（带符号，正负表示方向）。
 * 返回：0 成功；参数非法或底层调用失败返回 -1。
 * 说明：先按 ±max_speed 钳位再下发；仅当此前处于停止状态且新速度
 *       非 0 时才更新 direction，运行中反向不会改写 direction 字段。
 */
int motor_set_speed(motor_t *motor, int32_t speed)
{
    int32_t s;

    if (motor == NULL || motor->ops == NULL || motor->ops->set_speed == NULL) {
        return -1;
    }
    /* 限幅到 [-max_speed, max_speed] */
    s = speed;
    if (s > motor->max_speed) {
        s = motor->max_speed;
    } else if (s < -motor->max_speed) {
        s = -motor->max_speed;
    }
    if (motor->ops->set_speed(motor->ops->ctx, s) != 0) {
        return -1;
    }
    motor->speed = s;
    /* 由停止态启动时，根据符号推断方向 */
    if (s != 0 && motor->direction == MOTOR_STOPPED) {
        motor->direction = (s > 0) ? MOTOR_FORWARD : MOTOR_REVERSE;
    }
    return 0;
}

/*
 * 设置电机方向。
 * 参数：motor 对象指针；direction 目标方向枚举。
 * 返回：0 成功；参数非法或底层调用失败返回 -1。
 * 说明：仅调用底层 set_direction，不改变当前速度。
 */
int motor_set_direction(motor_t *motor, motor_direction_e direction)
{
    if (motor == NULL || motor->ops == NULL || motor->ops->set_direction == NULL) {
        return -1;
    }
    if (motor->ops->set_direction(motor->ops->ctx, direction) != 0) {
        return -1;
    }
    motor->direction = direction;
    return 0;
}

/*
 * 停止电机。
 * 参数：motor 对象指针。
 * 返回：0 成功；参数非法返回 -1。
 * 说明：优先调用底层 stop 回调；若未实现则退化为 set_speed(ctx, 0)。
 *       两个回调都为空时仍会把软件状态置为停止。
 */
int motor_stop(motor_t *motor)
{
    if (motor == NULL || motor->ops == NULL) {
        return -1;
    }
    if (motor->ops->stop != NULL) {
        motor->ops->stop(motor->ops->ctx);
    } else if (motor->ops->set_speed != NULL) {
        motor->ops->set_speed(motor->ops->ctx, 0);
    }
    motor->speed = 0;
    motor->direction = MOTOR_STOPPED;
    return 0;
}