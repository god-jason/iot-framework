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
 *   - step 回调必须实现，set_dir 可选；
 *   - move/move_to 为阻塞式执行，脉冲间隔由底层实现决定；
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
    stepper_cmd_add(st);
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

/*
 * 相对移动若干步（阻塞）。
 * 参数：st 对象指针；steps 步数（正=正向，负=反向）。
 * 返回：0 成功；参数非法、设方向失败或中途发脉冲失败返回 -1。
 * 说明：按符号确定方向后取绝对值逐步发脉冲；一旦某步失败立即返回，
 *       此时 position 只反映已成功发出的步数。
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