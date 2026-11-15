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
 *   - move/move_to 为阻塞式执行；未启用加减速时脉冲间隔由底层决定；
 *   - 启用加减速后按梯形速度曲线调整步间隔，避免骤起骤停丢步；
 *   - position 为 int32_t，长时间单向运行可能溢出；
 *   - steps 为 INT32_MIN 时取反会溢出，调用者应避免该极值。
 */
#include "stepper.h"
#include "ros.h"
#include "common.h"
#include "cmd.h"

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

static int stepper_cmd_handler(const args_parsed_t *parsed, void *user)
{
    uint32_t id;
    const char *action = NULL;
    const char *value = NULL;
    stepper_t *st;
    int32_t v = 0;
    int rc;

    (void)user;
    if (!args_get_uint(parsed, 0U, &id) || !args_get_str(parsed, 1U, &action)) {
        ros_puts("usage: stepper <id> dir|move|moveto|pos|info [value]");
        return -1;
    }
    if (id >= s_stepper_count || s_steppers[id] == NULL) {
        ros_printf("stepper %u not found\n", (unsigned)id);
        return -1;
    }
    st = s_steppers[id];
    (void)args_get_str(parsed, 2U, &value);

    if (str_icmp(action, "pos")) {
        int32_t pos = 0;

        (void)stepper_get_position(st, &pos);
        LOG_I("stepper", "stepper[%u] pos=%ld", (unsigned)id, (long)pos);
        return 0;
    }
    if (str_icmp(action, "info")) {
        LOG_I("stepper", "stepper[%u] pos=%ld dir=%u", (unsigned)id,
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

static const args_def_t s_stepper_args[] = {
    { "id", 0, ARGS_TYPE_UINT, true, true, NULL, "stepper instance id" },
    { "action", 0, ARGS_TYPE_STR, true, true, NULL,
      "dir|move|moveto|pos|info" },
    { "value", 0, ARGS_TYPE_STR, true, false, NULL, "steps / direction" }
};

static const cmd_def_t s_stepper_def = {
    "stepper", "stepper motion control", s_stepper_args, 3U
};

static void stepper_cmd_add(stepper_t *st)
{
    if (stepper_cmd_index(st) >= 0 || s_stepper_count >= STEPPER_CMD_MAX) {
        return;
    }
    s_steppers[s_stepper_count++] = st;
    if (!s_stepper_cmd_ready) {
        s_stepper_cmd_ready = true;
        (void)cmd_register(&s_stepper_def, stepper_cmd_handler, NULL);
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
 * 按梯形速度曲线发出 count 个脉冲（count > 0）。
 * 速度按 dv = a/v 逐步变化；同时用剩余步数约束减速度，
 * 保证在到达目标前能降到 start_speed，避免过冲/丢步。
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
        {
            uint32_t dv = st->accel / v;
            uint32_t v_next = v + ((dv == 0U) ? 1U : dv);
            uint64_t stop2;

            if (v_next > st->max_speed) {
                v_next = st->max_speed;
            }
            /* 剩余 remaining 步内必须能从 v_next 降到 start_speed */
            stop2 = (uint64_t)st->start_speed * st->start_speed +
                    2ULL * st->decel * remaining;
            if ((uint64_t)v_next * v_next > stop2) {
                /* 按停车距离反推允许速度：v <= sqrt(start² + 2*d*remaining) */
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
            v = (v_next == 0U) ? 1U : v_next;
        }
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