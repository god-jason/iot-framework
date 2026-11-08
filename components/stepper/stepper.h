#ifndef STEPPER_H
#define STEPPER_H

/*
 * 步进电机：方向 + 步进脉冲控制。底层 ops 由 GPIO/专用驱动实现。
 * 位置以「步数」为单位。
 *
 * 模块说明：
 *   - 每次移动通过循环发脉冲实现，属于阻塞式接口；
 *   - 同时维护软件侧绝对位置 position，便于 move_to 换算相对步数；
 *   - 方向取值 0（正向）/ 1（反向），仅最低位有效。
 * 使用约束：
 *   - step 回调必须实现，set_dir 可选（缺失时只记录软件方向）；
 *   - 脉冲频率与加减速完全由底层负责，本模块不插入延时。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 底层步进驱动操作表 */
typedef struct {
    int (*set_dir)(void *ctx, uint8_t dir);   /* 0=正向 1=反向，可选 */
    int (*step)(void *ctx);                   /* 发出一个步进脉冲，必须实现 */
    void *ctx;                                /* 驱动私有上下文 */
} stepper_ops_t;

/* 步进电机对象 */
typedef struct {
    const stepper_ops_t *ops;   /* 底层操作表 */

    int32_t position;           /* 当前绝对步位置（正向递增） */
    uint8_t direction;          /* 当前方向：0 正向 / 1 反向 */
} stepper_t;

/*
 * 初始化步进电机（位置清零、方向正向）。
 * 参数：st 对象；ops 操作表（step 必须非 NULL）。
 * 返回：0 成功；参数非法返回 -1。
 */
int stepper_init(stepper_t *st, const stepper_ops_t *ops);

/*
 * 设置方向。
 * 参数：st 对象；dir 方向（仅最低位有效）。
 * 返回：0 成功；参数非法或底层失败返回 -1。
 */
int stepper_set_direction(stepper_t *st, uint8_t dir);

/* 相对移动 steps 步（阻塞执行，带符号：正=正向 负=反向） */
int stepper_move(stepper_t *st, int32_t steps);

/* 移动到绝对目标位置（内部换算为相对步数） */
int stepper_move_to(stepper_t *st, int32_t target);

/* 读取当前位置：成功返回 0 并写入 *position */
int stepper_get_position(stepper_t *st, int32_t *position);

#ifdef __cplusplus
}
#endif

#endif /* STEPPER_H */