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
#include "ros.h"
#include "common.h"
#include "cmd.h"

#define SERVO_CMD_MAX 8U

static servo_t *s_servos[SERVO_CMD_MAX];
static uint8_t s_servo_count;
static bool s_servo_cmd_ready;

static int servo_cmd_index(const servo_t *servo)
{
    uint8_t i;

    for (i = 0U; i < s_servo_count; i++) {
        if (s_servos[i] == servo) {
            return (int)i;
        }
    }
    return -1;
}

static int servo_cmd_handler(const args_parsed_t *parsed, void *user)
{
    uint32_t id;
    uint32_t angle = 0U;
    servo_t *servo;

    (void)user;
    if (!args_get_uint(parsed, 0U, &id)) {
        ros_puts("usage: servo <id> [angle]");
        return -1;
    }
    if (id >= s_servo_count || s_servos[id] == NULL) {
        ros_printf("servo %u not found\n", (unsigned)id);
        return -1;
    }
    servo = s_servos[id];

    if (!args_get_uint(parsed, 1U, &angle)) {
        ros_printf("servo[%u] angle=%u range=%u..%u\n", (unsigned)id,
                   (unsigned)servo->angle, (unsigned)servo->min_angle,
                   (unsigned)servo->max_angle);
        return 0;
    }
    if (servo_set_angle(servo, (uint16_t)angle) != 0) {
        ros_printf("servo[%u] set angle failed\n", (unsigned)id);
        return -1;
    }
    return 0;
}

static const args_def_t s_servo_args[] = {
    { "id", 0, ARGS_TYPE_UINT, true, true, NULL, "servo instance id" },
    { "angle", 0, ARGS_TYPE_UINT, true, false, NULL, "target angle" }
};

static const cmd_def_t s_servo_def = {
    "servo", "servo angle control", s_servo_args, 2U
};

static void servo_cmd_add(servo_t *servo)
{
    if (servo_cmd_index(servo) >= 0 || s_servo_count >= SERVO_CMD_MAX) {
        return;
    }
    s_servos[s_servo_count++] = servo;
    if (!s_servo_cmd_ready) {
        s_servo_cmd_ready = true;
        (void)cmd_register(&s_servo_def, servo_cmd_handler, NULL);
    }
}

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
    servo_cmd_add(servo);
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