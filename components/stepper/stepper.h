#ifndef STEPPER_H
#define STEPPER_H

/*
 * 步进电机：方向 + 步进脉冲控制。底层 ops 由 GPIO/专用驱动实现。
 * 位置以「步数」为单位。
 *
 * 模块说明：
 *   - 同时维护软件侧绝对位置 position，便于 move_to 换算相对步数；
 *   - 方向取值 0（正向）/ 1（反向），仅最低位有效；
 *   - 可选加减速（默认关闭）：按梯形速度曲线逐步改变步间隔，
 *     避免骤起骤停导致丢步。
 *   - 启用加减速且配置了硬件定时器（stepper_set_timer）时，脉冲由
 *     定时器更新中断发出，采用与参考实现一致的 S 形速度曲线
 *     v = -2t³ + 3t²（缓起缓停）：速度按固定时间间隔
 *     （ramp_interval_us，默认 10ms）更新一次，每级速度步长恒定，
 *     加速时间只由目标速度差决定；回调内调整定时器周期改变脉冲频率；
 *     stepper_move 阻塞等待移动完成后返回。
 * 使用约束：
 *   - step 回调必须实现，set_dir / delay_us 可选；
 *   - 未启用加减速时，脉冲频率完全由底层决定，模块不插入延时；
 *   - 启用加减速但未挂定时器时退回软件延时：若提供 delay_us 则由
 *     底层精确延时，否则退回 ros_sleep_ms（精度约 1ms）；
 *   - 定时器加减速要求内核已启动（stepper_move 阻塞在信号量上），
 *     且平台 TIM 更新中断需把事件派发到句柄 callback。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "hal.h"
#include "ros.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 底层步进驱动操作表 */
typedef struct {
    int (*set_dir)(void *ctx, uint8_t dir);   /* 0=正向 1=反向，可选 */
    int (*step)(void *ctx);                   /* 发出一个步进脉冲，必须实现 */
    /* 可选：步间隔延时（微秒）。未提供时加减速退回 ros_sleep_ms */
    void (*delay_us)(void *ctx, uint32_t us);
    void *ctx;                                /* 驱动私有上下文 */
} stepper_ops_t;

/* 步进电机对象 */
typedef struct {
    const stepper_ops_t *ops;   /* 底层操作表 */

    int32_t position;           /* 当前绝对步位置（正向递增） */
    uint8_t direction;          /* 当前方向：0 正向 / 1 反向 */

    /* 加减速参数；max_speed 或 accel 为 0 表示关闭（默认） */
    uint32_t max_speed;         /* 最高速度，步/秒 */
    uint32_t start_speed;       /* 起始/停止速度，步/秒 */
    uint32_t accel;             /* 平均加速度，步/秒² */
    uint32_t decel;             /* 平均减速度，步/秒² */

    /* 硬件定时器加减速（可选，见 stepper_set_timer） */
    hal_tim_t *tim;             /* 定时器句柄；NULL 时加减速退回软件延时 */
    uint32_t tim_tick_hz;       /* 定时器分频后的计数时钟（Hz） */
    uint32_t ramp_interval_us;  /* 速度更新间隔（µs），默认 10000 */
    ros_sem_t ramp_done;        /* 一次加减速移动完成信号 */
    volatile uint32_t ramp_remaining; /* 剩余步数（中断上下文读写） */
    volatile uint32_t ramp_speed;     /* 当前脉冲速度，步/秒（中断上下文读写） */
    volatile int ramp_status;         /* 本次移动结果：0 成功 / -1 底层失败 */
    /* S 曲线运行状态（中断上下文读写） */
    volatile uint8_t ramp_phase;      /* 0 加速 / 1 匀速 / 2 减速 */
    volatile uint32_t ramp_idx;       /* 当前阶段已走过的更新次数 */
    volatile uint32_t ramp_steps;     /* 当前阶段总更新次数 */
    volatile uint32_t ramp_v_from;    /* 阶段起始速度 */
    volatile uint32_t ramp_v_to;      /* 阶段结束速度 */
    volatile uint32_t ramp_cruise_v;  /* 匀速段速度 */
    volatile uint32_t ramp_decel_dist;/* 剩余步数低于该值时进入减速 */
    volatile uint32_t ramp_pulse_acc; /* 间隔内脉冲数定点累加器 */
    volatile uint32_t ramp_pulses_left; /* 当前间隔应发脉冲数 */
    volatile uint32_t ramp_pulses_done;  /* 当前间隔已发脉冲数 */
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

/*
 * 配置加减速（默认关闭）。
 * 参数：max_speed 最高速度（步/秒）；start_speed 起停速度（0 自动取 1/4）；
 *       accel 加速度（步/秒²）；decel 减速度（0 表示与 accel 相同）。
 * 返回：0 成功；st 为 NULL 返回 -1。
 * 说明：max_speed 或 accel 为 0 时关闭加减速，恢复原有定速发脉冲行为。
 */
int stepper_set_ramp(stepper_t *st, uint32_t max_speed, uint32_t start_speed,
                     uint32_t accel, uint32_t decel);

/*
 * 挂接硬件定时器用于加减速脉冲生成。
 * 参数：tim 定时器句柄（NULL 表示卸载，加减速退回软件延时）；
 *       tick_hz 定时器分频后的计数时钟（Hz），决定周期换算精度。
 * 返回：0 成功；st 为 NULL 或 tim 非 NULL 而 tick_hz 为 0 返回 -1。
 * 说明：会把 tim 的 callback/user_data 绑定到本模块；定时器本身的
 *       初始化、时钟与预分频配置由调用者完成。
 */
int stepper_set_timer(stepper_t *st, hal_tim_t *tim, uint32_t tick_hz);

/*
 * 设置硬件定时器加减速的速度更新间隔（参考实现的 acc_interval）。
 * 参数：interval_us 间隔微秒（如 10000=10ms、1000=1ms）。
 * 返回：0 成功；st 为 NULL 或 interval_us 为 0 返回 -1。
 * 说明：默认 10000µs；间隔越小曲线越平滑，但中断内计算更频繁。
 */
int stepper_set_ramp_interval(stepper_t *st, uint32_t interval_us);

/*
 * 定时器更新中断入口（已由各平台 IRQ 派发接入，一般无需手动调用）。
 * 每次触发发出一个步进脉冲；每经过一个速度更新间隔（ramp_interval_us
 * 对应的脉冲数）沿 S 形曲线推进一档并重装定时器周期；
 * 移动结束时停止定时器并唤醒等待 stepper_move 的任务。
 * 说明：也可在主机测试中直接调用以模拟周期中断。
 */
void stepper_tim_irq(void *handle, hal_event_e event, void *user_data);

/* 移动到绝对目标位置（内部换算为相对步数） */
int stepper_move_to(stepper_t *st, int32_t target);

/* 读取当前位置：成功返回 0 并写入 *position */
int stepper_get_position(stepper_t *st, int32_t *position);

#ifdef __cplusplus
}
#endif

#endif /* STEPPER_H */