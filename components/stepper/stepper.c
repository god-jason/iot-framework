/*
 * 步进电机模块实现。
 *
 * 职责：
 *   以「方向 + 步进脉冲」的方式控制步进电机，并维护软件侧步位置。
 *
 * 数据流：
 *   init() 绑定 set_dir/step 回调 -> move(steps) 依据符号选方向并循环
 *   发脉冲，每发一步累加/累减 position -> move_to() 换算成相对步数。
 *
 * 依赖与边界：
 *   - step 回调必须实现，set_dir / delay_us 可选；
 *   - 启用加减速且挂接了硬件定时器（stepper_set_timer）时，脉冲在
 *     定时器更新中断中发出，速度按固定时间间隔沿 S 形曲线
 *     v = -2t³ + 3t² 更新（参考原 Lua 实现），回调内调整周期改变频率；
 *     stepper_move 阻塞到移动结束；
 *   - 启用加减速但未挂定时器时，move/move_to 仍为软件延时的阻塞执行；
 *   - 未启用加减速时保持原有定速发脉冲行为；
 *   - position 为 int32_t，长时间单向运行可能溢出；
 *   - steps 为 INT32_MIN 时取反会溢出，调用者应避免该极值。
 */
#include "stepper.h"
#include "ros.h"
#include "common.h"
#include "roshell.h"

#define STEPPER_CMD_MAX 8U

static stepper_t *s_steppers[STEPPER_CMD_MAX];
static uint8_t s_stepper_count;
static bool s_stepper_cmd_ready;

static int stepper_cmd_index(const stepper_t *st)
{
    uint8_t i;

    for (i = 0U; i < s_stepper_count; i++) {
        if (s_steppers[i] == st) {
            return (int)i;
        }
    }
    return -1;
}

static int stepper_cmd_handler(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)user;
    uint32_t id;
    const char *action = NULL;
    const char *value = NULL;
    stepper_t *st;
    int32_t v = 0;
    int rc;

    if (!rs_args_get_uint(parsed, 0U, &id) || !rs_args_get_str(parsed, 1U, &action)) {
        ros_puts("usage: stepper <id> dir|move|moveto|pos|info [value]");
        return -1;
    }
    if (id >= s_stepper_count || s_steppers[id] == NULL) {
        ros_printf("stepper %u not found\n", (unsigned)id);
        return -1;
    }
    st = s_steppers[id];
    (void)rs_args_get_str(parsed, 2U, &value);

    if (str_icmp(action, "pos")) {
        int32_t pos = 0;

        (void)stepper_get_position(st, &pos);
        ros_printf("stepper[%u] pos=%ld\n", (unsigned)id, (long)pos);
        return 0;
    }
    if (str_icmp(action, "info")) {
        ros_printf("stepper[%u] pos=%ld dir=%u\n", (unsigned)id,
                   (long)st->position, (unsigned)st->direction);
        return 0;
    }
    if (value == NULL || !str_parse_int(value, &v)) {
        ros_puts("usage: stepper <id> dir|move|moveto <value>");
        return -1;
    }
    if (str_icmp(action, "dir")) {
        rc = stepper_set_direction(st, (uint8_t)v);
    } else if (str_icmp(action, "move")) {
        rc = stepper_move(st, v);
    } else if (str_icmp(action, "moveto")) {
        rc = stepper_move_to(st, v);
    } else {
        ros_printf("stepper: unknown action '%s'\n", action);
        return -1;
    }

    if (rc != 0) {
        ros_printf("stepper[%u] %s failed\n", (unsigned)id, action);
        return -1;
    }
    return 0;
}

static const rs_args_t s_stepper_args[] = {
    { "id", 0, RS_ARGS_TYPE_UINT, true, true, NULL, "stepper instance id" },
    { "action", 0, RS_ARGS_TYPE_STR, true, true, NULL,
      "dir|move|moveto|pos|info" },
    { "value", 0, RS_ARGS_TYPE_STR, true, false, NULL, "steps / direction" }
};

static const rs_cmd_t s_stepper_def = {
    "stepper", "stepper motion control", NULL, NULL, s_stepper_args, 3U,
    stepper_cmd_handler, NULL
};

static void stepper_cmd_add(stepper_t *st)
{
    if (stepper_cmd_index(st) >= 0 || s_stepper_count >= STEPPER_CMD_MAX) {
        return;
    }
    s_steppers[s_stepper_count++] = st;
    if (!s_stepper_cmd_ready) {
        s_stepper_cmd_ready = true;
        (void)rs_cmd_register(&s_stepper_def);
    }
}

/*
 * 初始化步进电机对象。
 * 参数：st 对象指针（非 NULL）；ops 操作表（step 必须实现）。
 * 返回：0 成功；参数非法返回 -1。
 * 说明：位置清零、方向置 0（正向）。
 */
int stepper_init(stepper_t *st, const stepper_ops_t *ops)
{
    if (st == NULL || ops == NULL || ops->step == NULL) {
        return -1;
    }
    st->ops = ops;
    st->position = 0;
    st->direction = 0;
    /* 加减速默认关闭 */
    st->max_speed = 0U;
    st->start_speed = 0U;
    st->accel = 0U;
    st->decel = 0U;
    /* 硬件定时器加减速：默认未挂接，速度更新间隔 10ms */
    st->tim = NULL;
    st->tim_tick_hz = 0U;
    st->ramp_interval_us = 10000U;
    st->ramp_remaining = 0U;
    st->ramp_speed = 0U;
    st->ramp_status = 0;
    st->ramp_phase = 0U;
    st->ramp_idx = 0U;
    st->ramp_steps = 0U;
    st->ramp_v_from = 0U;
    st->ramp_v_to = 0U;
    st->ramp_cruise_v = 0U;
    st->ramp_decel_dist = 0U;
    st->ramp_pulse_acc = 0U;
    st->ramp_pulses_left = 0U;
    st->ramp_pulses_done = 0U;
    (void)ros_sem_init(&st->ramp_done, 0U);
    stepper_cmd_add(st);
    return 0;
}

/*
 * 配置加减速参数。
 * max_speed 或 accel 为 0 时关闭；start_speed 为 0 时自动取 max_speed/4。
 */
int stepper_set_ramp(stepper_t *st, uint32_t max_speed, uint32_t start_speed,
                     uint32_t accel, uint32_t decel)
{
    if (st == NULL) {
        return -1;
    }
    if (max_speed == 0U || accel == 0U) {
        st->max_speed = 0U;
        st->start_speed = 0U;
        st->accel = 0U;
        st->decel = 0U;
        return 0;
    }
    if (start_speed == 0U) {
        start_speed = max_speed / 4U;
    }
    if (start_speed == 0U) {
        start_speed = 1U;
    }
    if (start_speed > max_speed) {
        start_speed = max_speed;
    }
    if (decel == 0U) {
        decel = accel;
    }
    st->max_speed = max_speed;
    st->start_speed = start_speed;
    st->accel = accel;
    st->decel = decel;
    return 0;
}

/*
 * 设置步进方向。
 * 参数：st 对象指针；dir 方向（仅最低位有效：0=正向，1=反向）。
 * 返回：0 成功；参数非法或底层 set_dir 失败返回 -1。
 * 说明：底层未实现 set_dir 时只记录软件方向。
 */
int stepper_set_direction(stepper_t *st, uint8_t dir)
{
    if (st == NULL) {
        return -1;
    }
    /* 只保留最低位，避免非法方向值 */
    st->direction = dir & 1u;
    if (st->ops->set_dir != NULL) {
        if (st->ops->set_dir(st->ops->ctx, st->direction) != 0) {
            return -1;
        }
    }
    return 0;
}

/* 整数平方根（用于由停车距离约束反推允许的最高速度） */
static uint32_t stepper_isqrt(uint64_t x)
{
    uint64_t r = 0U;
    uint64_t bit = 1ULL << 62;

    while (bit > x) {
        bit >>= 2;
    }
    while (bit != 0U) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)r;
}

/*
 * 按梯形曲线计算下一步的速度。
 * 参数：v 当前速度（步/秒）；remaining 本次脉冲之后剩余的步数。
 * 说明：先按加速度提升（dv = accel / v），再按停车距离约束：
 *       剩余 remaining 步内必须能从 v_next 降到 start_speed，
 *       即 v_next <= sqrt(start_speed² + 2 * decel * remaining)。
 */
static uint32_t stepper_next_speed(const stepper_t *st, uint32_t v,
                                   uint32_t remaining)
{
    uint32_t dv = st->accel / v;
    uint32_t v_next = v + ((dv == 0U) ? 1U : dv);
    uint64_t stop2;

    if (v_next > st->max_speed) {
        v_next = st->max_speed;
    }
    stop2 = (uint64_t)st->start_speed * st->start_speed +
            2ULL * st->decel * remaining;
    if ((uint64_t)v_next * v_next > stop2) {
        /* 按停车距离反推允许速度 */
        uint32_t v_allow = stepper_isqrt(stop2);

        if (v_allow > st->max_speed) {
            v_allow = st->max_speed;
        }
        if (v_allow < st->start_speed) {
            v_allow = st->start_speed;
        }
        if (v_next > v_allow) {
            v_next = v_allow;
        }
    }
    return (v_next == 0U) ? 1U : v_next;
}

/*
 * 速度换算为定时器周期（计数 tick 数）：period = tick_hz / 速度，
 * 至少为 1（速度超过计数时钟时按每 tick 一步封顶）。
 */
static uint32_t stepper_tim_period(const stepper_t *st, uint32_t speed)
{
    uint32_t p;

    if (speed == 0U) {
        speed = 1U;
    }
    p = st->tim_tick_hz / speed;
    return (p == 0U) ? 1U : p;
}

/*
 * S 形速度曲线插值（参考实现 v = -2t³ + 3t²，缓起缓停）。
 * 参数：idx/steps 为归一化进度，毫度定点计算。
 */
static uint32_t stepper_scurve(uint32_t v_from, uint32_t v_to, uint32_t idx,
                               uint32_t steps)
{
    uint32_t x;
    uint32_t s;
    uint32_t diff;
    uint32_t dv;

    if (steps == 0U || idx >= steps) {
        return v_to;
    }
    x = idx * 1000U / steps;                    /* 0..999 */
    s = (x * x * (3000U - 2U * x)) / 1000000U;  /* 0..1000，毫度 */
    if (v_to >= v_from) {
        diff = v_to - v_from;
        dv = diff * s / 1000U;
        return v_from + dv;
    }
    diff = v_from - v_to;
    dv = diff * s / 1000U;
    return v_from - dv;
}

/* 每个速度更新区间的速度步长：平均加速度 a 对应每档增量 */
static uint32_t stepper_v_step(uint32_t a, uint32_t interval_us)
{
    uint32_t v = (uint32_t)((uint64_t)a * interval_us / 1000000ULL);

    return (v == 0U) ? 1U : v;
}

/*
 * 计算一个速度更新区间内应发的脉冲数（定点累加，低速不丢脉冲）。
 * 说明：定时器每拍发一个脉冲，脉冲周期 1/v，因此每区间约 v*interval 个脉冲。
 */
static uint32_t stepper_interval_pulses(stepper_t *st, uint32_t v)
{
    uint64_t acc;
    uint32_t n;

    acc = (uint64_t)v * st->ramp_interval_us + st->ramp_pulse_acc;
    n = (uint32_t)(acc / 1000000ULL);
    st->ramp_pulse_acc = (uint32_t)(acc % 1000000ULL);
    return (n == 0U) ? 1U : n;
}

/*
 * 移动开始时规划 S 曲线：巡航速度、加速档数与减速距离。
 * 说明：S 曲线段平均速度为 (v0+v1)/2，距离公式与梯形一致；
 *       加速时间 = 速度差 / 每档速度步长，只由目标速度差决定，
 *       避免了逐脉冲 dv=a/v 造成低速档跳变大、高速档爬升慢。
 */
static void stepper_ramp_plan(stepper_t *st, uint32_t count)
{
    uint32_t vs = st->start_speed;
    uint32_t vc;
    uint32_t va;
    uint32_t vd;
    uint32_t diff;
    uint64_t reach;
    uint64_t v2;

    va = stepper_v_step(st->accel, st->ramp_interval_us);
    vd = stepper_v_step(st->decel, st->ramp_interval_us);

    if (st->max_speed <= vs) {
        vc = vs;
    } else {
        /* 从 vs 加速到 max_speed 所需脉冲数 */
        reach = ((uint64_t)st->max_speed * st->max_speed - (uint64_t)vs * vs) *
                ((uint64_t)st->accel + st->decel) /
                (2ULL * st->accel * st->decel);
        if (count >= reach) {
            vc = st->max_speed;
        } else {
            /* 行程不足以到顶速：三角形曲线，按总距离反推峰值 */
            v2 = (uint64_t)vs * vs +
                 2ULL * count * st->accel * st->decel /
                 ((uint64_t)st->accel + st->decel);
            vc = stepper_isqrt(v2);
            if (vc > st->max_speed) {
                vc = st->max_speed;
            }
            if (vc < vs) {
                vc = vs;
            }
        }
    }

    st->ramp_cruise_v = vc;
    st->ramp_speed = vs;
    st->ramp_pulse_acc = 0U;
    st->ramp_pulses_done = 0U;
    st->ramp_idx = 0U;
    if (vc > vs) {
        diff = vc - vs;
        st->ramp_phase = 0U;      /* 加速段 */
        st->ramp_steps = (diff + va - 1U) / va;
        st->ramp_v_from = vs;
        st->ramp_v_to = vc;
        /* 减速距离 = 平均速度 * 减速时长，再加 1/4 余量提前起减，
         * 保证曲线在行程结束前减回 start_speed（余量段匀速） */
        st->ramp_decel_dist =
            (uint32_t)((uint64_t)(vs + vc) * ((diff + vd - 1U) / vd) *
                       st->ramp_interval_us / 2000000ULL);
        st->ramp_decel_dist += st->ramp_decel_dist / 4U + 1U;
    } else {
        st->ramp_phase = 1U;      /* 直接以 start_speed 匀速 */
        st->ramp_steps = 0U;
        st->ramp_v_from = vs;
        st->ramp_v_to = vs;
        st->ramp_decel_dist = 0U;
    }
    st->ramp_pulses_left = stepper_interval_pulses(st, vs);
}

/*
 * 每个速度更新区间结束推进一次 S 曲线，重装定时器周期并计算
 * 下一区间的脉冲数（在定时器中断上下文执行）。
 */
static void stepper_ramp_advance(stepper_t *st)
{
    uint32_t v = st->ramp_speed;

    /* 剩余脉冲进入减速距离：立即从当前速度沿 S 曲线减回 start_speed。
     * 每个区间边界都检查（含加速段），避免高速段脉冲/档粒度误差
     * 导致曲线到行程结束仍未减完 */
    if (st->ramp_phase != 2U && st->ramp_decel_dist != 0U &&
        st->ramp_remaining <= st->ramp_decel_dist) {
        uint32_t vd = stepper_v_step(st->decel, st->ramp_interval_us);
        uint32_t diff = v - st->start_speed;

        if (diff > 0U) {
            st->ramp_phase = 2U;
            st->ramp_idx = 0U;
            st->ramp_steps = (diff + vd - 1U) / vd;
            st->ramp_v_from = v;
            st->ramp_v_to = st->start_speed;
        } else {
            st->ramp_phase = 1U;
        }
    }

    switch (st->ramp_phase) {
    case 0U:     /* 加速段 */
        st->ramp_idx++;
        if (st->ramp_idx >= st->ramp_steps) {
            st->ramp_phase = 1U;
            v = st->ramp_cruise_v;
        } else {
            v = stepper_scurve(st->ramp_v_from, st->ramp_v_to,
                               st->ramp_idx, st->ramp_steps);
        }
        break;
    case 1U:     /* 匀速段 */
        break;
    case 2U:     /* 减速段：走完档数后保持 start_speed 直到结束 */
        st->ramp_idx++;
        v = stepper_scurve(st->ramp_v_from, st->ramp_v_to,
                           st->ramp_idx, st->ramp_steps);
        break;
    default:
        break;
    }
    if (v == 0U) {
        v = 1U;
    }
    st->ramp_speed = v;
    (void)hal_tim_set_period(st->tim, stepper_tim_period(st, v));
    st->ramp_pulses_left = stepper_interval_pulses(st, v);
    st->ramp_pulses_done = 0U;
}

/*
 * 按梯形速度曲线发出 count 个脉冲（count > 0，软件延时方式）。
 * 速度按 stepper_next_speed() 逐步变化，步间隔由当前速度换算。
 */
static int stepper_move_ramp(stepper_t *st, uint32_t count, uint8_t dir)
{
    uint32_t v = st->start_speed;
    uint32_t remaining = count;
    uint32_t us_acc = 0U;

    while (remaining > 0U) {
        if (v == 0U) {
            v = 1U;
        }
        if (st->ops->step(st->ops->ctx) != 0) {
            LOG_E("stepper", "step fail");
            return -1;
        }
        st->position += (dir == 0) ? 1 : -1;
        remaining--;
        if (remaining == 0U) {
            break;              /* 最后一步不再等待 */
        }

        /* 步间隔由当前速度决定，随后再更新速度 */
        {
            uint32_t period_us = 1000000U / v;

            if (st->ops->delay_us != NULL) {
                st->ops->delay_us(st->ops->ctx, period_us);
            } else {
                us_acc += period_us;
                if (us_acc >= 1000U) {
                    ros_sleep_ms(us_acc / 1000U);
                    us_acc %= 1000U;
                }
            }
        }

        /* 计算下一速度：先按加速度提升，再按剩余距离限制 */
        v = stepper_next_speed(st, v, remaining);
    }
    return 0;
}

/*
 * 定时器更新中断入口：发出一个步进脉冲并按梯形曲线调整下一步周期。
 * 说明：运行于中断上下文，只做发脉冲/改周期/结束唤醒三件事；
 *       ramp_remaining 为 0 表示不在移动中，忽略伪中断。
 */
void stepper_tim_irq(void *handle, hal_event_e event, void *user_data)
{
    stepper_t *st = (stepper_t *)user_data;
    uint32_t remaining;
    bool need_yield = false;

    (void)handle;
    (void)event;
    if (st == NULL || st->tim == NULL) {
        return;
    }
    remaining = st->ramp_remaining;
    if (remaining == 0U) {
        return;
    }
    if (st->ops->step(st->ops->ctx) != 0) {
        /* 底层发脉冲失败：立即停定时器并结束本次移动 */
        st->ramp_status = -1;
        st->ramp_remaining = 0U;
        (void)hal_tim_stop_it(st->tim);
        (void)ros_sem_give_from_isr(&st->ramp_done, &need_yield);
        if (need_yield) {
            ros_yield_from_isr();
        }
        return;
    }
    st->position += (st->direction == 0U) ? 1 : -1;
    remaining--;
    st->ramp_remaining = remaining;
    if (remaining == 0U) {
        (void)hal_tim_stop_it(st->tim);
        (void)ros_sem_give_from_isr(&st->ramp_done, &need_yield);
        if (need_yield) {
            ros_yield_from_isr();
        }
        return;
    }
    /* 按固定时间间隔推进 S 曲线：区间脉冲发完才更新一次脉冲频率 */
    st->ramp_pulses_done++;
    if (st->ramp_pulses_done >= st->ramp_pulses_left) {
        stepper_ramp_advance(st);
    }
}

/*
 * 硬件定时器方式执行加减速移动（count > 0，方向已提前设置）。
 * 流程：规划 S 曲线 -> 按 start_speed 装周期 -> 启动更新中断 ->
 *       阻塞等完成信号 -> 兜底停定时器并返回结果。
 */
static int stepper_move_timed(stepper_t *st, uint32_t count)
{
    st->ramp_status = 0;
    stepper_ramp_plan(st, count);
    /* 最后置剩余步数：非 0 后中断回调才开始响应 */
    st->ramp_remaining = count;

    (void)hal_tim_set_period(st->tim,
                             stepper_tim_period(st, st->ramp_speed));
    (void)hal_tim_set_counter(st->tim, 0U);
    if (hal_tim_start_it(st->tim) != HAL_OK) {
        st->ramp_remaining = 0U;
        LOG_E("stepper", "tim start fail");
        return -1;
    }
    (void)ros_sem_take(&st->ramp_done, ROS_TIMEOUT_FOREVER);
    /* 正常路径回调已停；失败/竞态路径兜底 */
    (void)hal_tim_stop_it(st->tim);
    return st->ramp_status;
}

/*
 * 设置速度更新间隔（µs），如 10000=10ms、1000=1ms。
 */
int stepper_set_ramp_interval(stepper_t *st, uint32_t interval_us)
{
    if (st == NULL || interval_us == 0U) {
        return -1;
    }
    st->ramp_interval_us = interval_us;
    return 0;
}

/*
 * 挂接/卸载加减速用硬件定时器。
 * 挂接时把句柄的 callback/user_data 绑定到本模块，
 * 平台 TIM 更新中断派发到 stepper_tim_irq()。
 */
int stepper_set_timer(stepper_t *st, hal_tim_t *tim, uint32_t tick_hz)
{
    if (st == NULL || (tim != NULL && tick_hz == 0U)) {
        return -1;
    }
    st->tim = tim;
    st->tim_tick_hz = (tim != NULL) ? tick_hz : 0U;
    if (tim != NULL) {
        tim->callback = stepper_tim_irq;
        tim->user_data = st;
    }
    return 0;
}

/*
 * 相对移动若干步（阻塞）。
 * 参数：st 对象指针；steps 步数（正=正向，负=反向）。
 * 返回：0 成功；参数非法、设方向失败或中途发脉冲失败返回 -1。
 * 说明：启用加减速时走梯形曲线，否则保持原有定速方式；
 *       一旦某步失败立即返回，position 只反映已成功发出的步数。
 */
int stepper_move(stepper_t *st, int32_t steps)
{
    int32_t i;
    uint8_t dir;

    if (st == NULL) {
        return -1;
    }
    /* 正数走正向(0)，负数走反向(1) */
    dir = (steps >= 0) ? 0 : 1;
    if (stepper_set_direction(st, dir) != 0) {
        return -1;
    }
    if (steps < 0) {
        steps = -steps;   /* 注意：INT32_MIN 取反会溢出 */
    }
    if (st->max_speed != 0U && st->accel != 0U && steps > 1) {
        if (st->tim != NULL) {
            /* 硬件定时器：更新中断发脉冲并调整频率 */
            return stepper_move_timed(st, (uint32_t)steps);
        }
        return stepper_move_ramp(st, (uint32_t)steps, dir);
    }
    for (i = 0; i < steps; i++) {
        if (st->ops->step(st->ops->ctx) != 0) {
            LOG_E("stepper", "step fail");   /* 步进脉冲输出失败 */
            return -1;
        }
        /* 正向加 1、反向减 1 维护软件位置 */
        st->position += (dir == 0) ? 1 : -1;
    }
    return 0;
}

/*
 * 移动到绝对位置。
 * 参数：st 对象指针；target 目标步位置。
 * 返回：stepper_move() 的返回值；st 为 NULL 返回 -1。
 * 说明：内部换算为相对步数 target - position 后复用 move()。
 */
int stepper_move_to(stepper_t *st, int32_t target)
{
    if (st == NULL) {
        return -1;
    }
    return stepper_move(st, target - st->position);
}

/*
 * 读取当前位置。
 * 参数：st 对象指针；position 输出缓冲。
 * 返回：0 成功；参数非法返回 -1。
 */
int stepper_get_position(stepper_t *st, int32_t *position)
{
    if (st == NULL || position == NULL) {
        return -1;
    }
    *position = st->position;
    return 0;
}