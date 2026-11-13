/*
 * PWM 风扇模块实现。
 *
 * 职责：
 *   把上层给出的 0..1000 档位（对应 0..100% 占空比）转发给底层 PWM，
 *   提供开/关两个语义化接口，并根据 TACH 反馈脉冲计算转速（RPM）。
 *
 * 数据流：
 *   init() 绑定 set_duty 回调并立即关断 -> set_speed() 限幅后输出，
 *   同时缓存当前档位；on()/off() 分别等价于 1000/0 档。
 *   测速：tach_pulse()/tach_sample() 累计脉冲 -> update_rpm() 在窗口
 *   结束时换算 rpm = pulses * 60000 / (ppr * elapsed_ms)。
 *
 * 依赖与边界：
 *   - 仅依赖 标准头文件 与日志宏，无静态状态；
 *   - 档位上限硬编码为 1000，超过时自动钳位（不会报错）；
 *   - rpm 计算使用 64 位中间量，避免高速时 32 位溢出；
 *   - now_ms 允许回绕，差值按无符号减法处理。
 */
#include "fan.h"
#include "ros.h"
#include "common.h"
#include "cmd.h"

#define FAN_CMD_MAX 8U

static fan_t *s_fans[FAN_CMD_MAX];
static uint8_t s_fan_count;
static bool s_fan_cmd_ready;

static int fan_cmd_index(const fan_t *fan)
{
    uint8_t i;

    for (i = 0U; i < s_fan_count; i++) {
        if (s_fans[i] == fan) {
            return (int)i;
        }
    }
    return -1;
}

static int fan_cmd_handler(const args_parsed_t *parsed, void *user)
{
    uint32_t id;
    uint32_t duty = 0U;
    fan_t *fan;

    (void)user;
    if (!args_get_uint(parsed, 0U, &id)) {
        ros_puts("usage: fan <id> [duty]");
        return -1;
    }
    if (id >= s_fan_count || s_fans[id] == NULL) {
        ros_printf("fan %u not found\n", (unsigned)id);
        return -1;
    }
    fan = s_fans[id];

    if (!args_get_uint(parsed, 1U, &duty)) {
        ros_printf("fan[%u] duty=%u rpm=%u\n", (unsigned)id,
                   (unsigned)fan->duty, (unsigned)fan->rpm);
        return 0;
    }
    if (fan_set_speed(fan, (uint16_t)duty) != 0) {
        ros_printf("fan[%u] set duty failed\n", (unsigned)id);
        return -1;
    }
    return 0;
}

static const args_def_t s_fan_args[] = {
    { "id", 0, ARGS_TYPE_UINT, true, true, NULL, "fan instance id" },
    { "duty", 0, ARGS_TYPE_UINT, true, false, NULL, "duty 0..1000" }
};

static const cmd_def_t s_fan_def = {
    "fan", "fan duty / rpm", s_fan_args, 2U
};

static void fan_cmd_add(fan_t *fan)
{
    if (fan_cmd_index(fan) >= 0 || s_fan_count >= FAN_CMD_MAX) {
        return;
    }
    s_fans[s_fan_count++] = fan;
    if (!s_fan_cmd_ready) {
        s_fan_cmd_ready = true;
        (void)cmd_register(&s_fan_def, fan_cmd_handler, NULL);
    }
}

/*
 * 初始化风扇。
 * 参数：fan 对象指针（非 NULL）；ops 操作表（set_duty 必须实现）。
 * 返回：fan_off() 的返回值；参数非法返回 -1。
 * 说明：初始化成功后会立即以 0 档输出，保证上电默认停转；
 *       测速状态一并清零，每转脉冲数取默认值。
 */
int fan_init(fan_t *fan, const fan_ops_t *ops)
{
    if (fan == NULL || ops == NULL || ops->set_duty == NULL) {
        return -1;
    }
    fan->ops = ops;
    fan->duty = 0;
    fan->pulses_per_rev = FAN_TACH_PPR_DEFAULT;
    fan->pulse_count = 0;
    fan->window_pulses = 0;
    fan->window_start_ms = 0;
    fan->rpm = 0;
    fan->tach_started = false;
    fan->tach_last_level = 0;
    fan_cmd_add(fan);
    return fan_off(fan);
}

/*
 * 设置风扇档位。
 * 参数：fan 对象指针；duty 目标档位（0..1000，超出按 1000 处理）。
 * 返回：底层 set_duty 的返回值；fan 为 NULL 返回 -1。
 */
int fan_set_speed(fan_t *fan, uint16_t duty)
{
    if (fan == NULL) {
        return -1;
    }
    /* 上限钳位：1000 对应 100% 占空比 */
    if (duty > 1000) {
        duty = 1000;
    }
    fan->duty = duty;
    return fan->ops->set_duty(fan->ops->ctx, duty);
}

/*
 * 全速开启风扇。
 * 参数：fan 对象指针。
 * 返回：fan_set_speed(fan, 1000) 的返回值；fan 为 NULL 返回 -1。
 */
int fan_on(fan_t *fan)
{
    if (fan == NULL) {
        return -1;
    }
    LOG_I("fan", "on");
    return fan_set_speed(fan, 1000);
}

/*
 * 关闭风扇。
 * 参数：fan 对象指针。
 * 返回：fan_set_speed(fan, 0) 的返回值；fan 为 NULL 返回 -1。
 */
int fan_off(fan_t *fan)
{
    if (fan == NULL) {
        return -1;
    }
    return fan_set_speed(fan, 0);
}

/*
 * 设置每转脉冲数。
 * 参数：fan 对象；ppr 每转 TACH 脉冲数（必须 > 0）。
 * 返回：0 成功；参数非法返回 -1。
 * 说明：修改后清空最近转速，下一个窗口按新参数计算。
 */
int fan_set_pulses_per_rev(fan_t *fan, uint16_t ppr)
{
    if (fan == NULL || ppr == 0U) {
        return -1;
    }
    fan->pulses_per_rev = ppr;
    fan->rpm = 0;
    return 0;
}

/*
 * 记录一个 TACH 脉冲。
 * 参数：fan 对象（NULL 时忽略）。
 * 说明：只做计数，供中断服务程序调用。
 */
void fan_tach_pulse(fan_t *fan)
{
    if (fan == NULL) {
        return;
    }
    fan->pulse_count++;
    fan->window_pulses++;
}

/*
 * 周期采样 TACH 电平并刷新转速。
 * 参数：fan 对象；level 当前电平（非 0 视为高）；now_ms 毫秒时基。
 * 返回：当前 RPM；fan 为 NULL 返回 0。
 * 说明：仅识别下降沿（1 -> 0），首次调用建立时间基准。
 */
uint32_t fan_tach_sample(fan_t *fan, uint8_t level, uint32_t now_ms)
{
    if (fan == NULL) {
        return 0;
    }
    level = (level != 0U) ? 1U : 0U;
    if (fan->tach_last_level != 0U && level == 0U) {
        fan_tach_pulse(fan);
    }
    fan->tach_last_level = level;
    return fan_update_rpm(fan, now_ms);
}

/*
 * 按测量窗口刷新转速。
 * 参数：fan 对象；now_ms 毫秒时基。
 * 返回：当前 RPM；窗口未结束时返回上一次结果。
 * 说明：rpm = 窗口脉冲数 * 60000 / (每转脉冲数 * 窗口时长ms)。
 *       窗口内无脉冲时结果为 0；时基回绕由无符号减法处理。
 */
uint32_t fan_update_rpm(fan_t *fan, uint32_t now_ms)
{
    uint32_t elapsed;
    uint64_t denom;
    uint64_t numer;

    if (fan == NULL || fan->pulses_per_rev == 0U) {
        return 0;
    }
    if (!fan->tach_started) {
        fan->tach_started = true;
        fan->window_start_ms = now_ms;
        return fan->rpm;
    }

    elapsed = now_ms - fan->window_start_ms;
    if (elapsed < FAN_TACH_WINDOW_MS) {
        return fan->rpm;
    }

    numer = (uint64_t)fan->window_pulses * 60000U;
    denom = (uint64_t)fan->pulses_per_rev * (uint64_t)elapsed;
    fan->rpm = (uint32_t)(numer / denom);

    fan->window_pulses = 0U;
    fan->window_start_ms = now_ms;
    return fan->rpm;
}

/*
 * 读取最近一次计算的转速。
 * 参数：fan 对象。
 * 返回：RPM；fan 为 NULL 返回 0。
 */
uint32_t fan_get_rpm(const fan_t *fan)
{
    return (fan != NULL) ? fan->rpm : 0U;
}

/*
 * 读取上电累计 TACH 脉冲数。
 * 参数：fan 对象。
 * 返回：脉冲计数；fan 为 NULL 返回 0。
 */
uint32_t fan_get_pulse_count(const fan_t *fan)
{
    return (fan != NULL) ? fan->pulse_count : 0U;
}