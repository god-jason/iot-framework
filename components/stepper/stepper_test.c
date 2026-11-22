/*
 * stepper_test.c - 步进电机测试（对应 stepper.c）
 *
 * 覆盖：初始化校验、方向设置、相对/绝对移动、位置读取、
 *       回调缺失与底层失败、软件加减速、硬件定时器加减速。
 */

#include "test.h"
#include "stepper.h"
#include "ros.h"

/*
 * 定时器加减速路径会阻塞在 ramp_done 信号量上，所以由低优先级任务发起移动，
 * 测试任务手动触发定时器回调逐拍驱动（PC 平台的 start_it 只同步触发一次）。
 */
static ros_task_t s_tim_task;
static uint8_t    s_tim_stack[1024];
static stepper_t *s_tim_owner;
static int32_t    s_tim_steps;
static int        s_tim_rc;
static uint32_t   s_tim_min_period;
static volatile bool s_tim_done;

static void tim_move_entry(void *arg)
{
    (void)arg;
    s_tim_rc = stepper_move(s_tim_owner, s_tim_steps);
    s_tim_done = true;
}

/* 发起一次定时器驱动的移动并泵脉冲；返回 stepper_move 的结果 */
static int tim_drive(stepper_t *st, hal_tim_t *tim, int32_t steps,
                     uint32_t max_pump)
{
    uint32_t i;

    s_tim_owner = st;
    s_tim_steps = steps;
    s_tim_rc = -1000;
    s_tim_min_period = 0xFFFFFFFFU;
    s_tim_done = false;
    if (ros_task_create(&s_tim_task, "stp_tim", 5U, tim_move_entry, NULL,
                        s_tim_stack, sizeof(s_tim_stack)) != ROS_OK) {
        return -1000;
    }
    for (i = 0U; i < max_pump && !s_tim_done; i++) {
        if (st->ramp_remaining != 0U) {
            if (tim->config.period < s_tim_min_period) {
                s_tim_min_period = tim->config.period;
            }
            stepper_tim_irq(tim, HAL_EVENT_COMPLETE, st);
        } else {
            ros_sleep_ms(1U);      /* 让出 CPU：等待任务启动/收尾 */
        }
    }
    if (s_tim_done) {
        ros_sleep_ms(2U);      /* 等待移动任务真正退出，再复用任务控制块 */
    }
    return s_tim_done ? s_tim_rc : -1001;
}

typedef struct {
    int steps;
    int last_dir;
    int step_fail;

    uint64_t time_us;       /* 累计延时 */
    uint32_t delay_calls;
    uint32_t first_us;
    uint32_t last_us;
    uint32_t min_us;
    uint32_t max_us;
} step_sim_t;

static int sim_set_dir(void *ctx, uint8_t dir)
{
    step_sim_t *s = (step_sim_t *)ctx;
    s->last_dir = (int)dir;
    return 0;
}

static int sim_step(void *ctx)
{
    step_sim_t *s = (step_sim_t *)ctx;
    if (s->step_fail) {
        return -1;
    }
    s->steps++;
    return 0;
}

static void sim_delay_us(void *ctx, uint32_t us)
{
    step_sim_t *s = (step_sim_t *)ctx;

    s->time_us += us;
    if (s->delay_calls == 0U) {
        s->first_us = us;
        s->min_us = us;
    }
    if (us < s->min_us) {
        s->min_us = us;
    }
    if (us > s->max_us) {
        s->max_us = us;
    }
    s->last_us = us;
    s->delay_calls++;
}

void ut_stepper(void)
{
    step_sim_t sim;
    stepper_t st;
    stepper_ops_t ops;
    int32_t pos = 0;

    memset(&sim, 0, sizeof(sim));
    memset(&ops, 0, sizeof(ops));
    ops.set_dir = sim_set_dir;
    ops.step = sim_step;
    ops.delay_us = sim_delay_us;
    ops.ctx = &sim;

    tf_begin("stepper/初始化");

    TF_CHECK_INT(stepper_init(NULL, &ops), -1);
    TF_CHECK_INT(stepper_init(&st, NULL), -1);
    {
        stepper_ops_t bad;
        bad.set_dir = sim_set_dir;
        bad.step = NULL;
        bad.ctx = &sim;
        TF_CHECK_INT(stepper_init(&st, &bad), -1);
    }
    TF_CHECK_INT(stepper_init(&st, &ops), 0);
    TF_CHECK_INT(st.position, 0);
    TF_CHECK_INT(st.direction, 0);

    tf_end();

    tf_begin("stepper/方向与相对移动");

    TF_CHECK_INT(stepper_set_direction(&st, 1U), 0);
    TF_CHECK_INT(st.direction, 1);
    TF_CHECK_INT(sim.last_dir, 1);

    TF_CHECK_INT(stepper_move(&st, 10), 0);
    TF_CHECK_INT(sim.steps, 10);
    TF_CHECK_INT(st.position, 10);

    TF_CHECK_INT(stepper_move(&st, -4), 0);          /* 负步数自动反向 */
    TF_CHECK_INT(sim.steps, 14);
    TF_CHECK_INT(st.position, 6);
    TF_CHECK_INT(st.direction, 1);

    TF_CHECK_INT(stepper_get_position(&st, &pos), 0);
    TF_CHECK_INT(pos, 6);

    tf_end();

    tf_begin("stepper/绝对移动与失败路径");

    TF_CHECK_INT(stepper_move_to(&st, 20), 0);
    TF_CHECK_INT(sim.steps, 28);
    TF_CHECK_INT(st.position, 20);

    TF_CHECK_INT(stepper_move_to(&st, 20), 0);       /* 已在目标位置 */
    TF_CHECK_INT(sim.steps, 28);

    sim.step_fail = 1;
    TF_CHECK_INT(stepper_move(&st, 3), -1);
    sim.step_fail = 0;

    TF_CHECK_INT(stepper_move(NULL, 1), -1);
    TF_CHECK_INT(stepper_move_to(NULL, 1), -1);
    TF_CHECK_INT(stepper_get_position(&st, NULL), -1);
    TF_CHECK_INT(stepper_set_direction(NULL, 0U), -1);
    TF_CHECK_INT(stepper_set_ramp(NULL, 1000U, 100U, 2000U, 2000U), -1);

    /* 默认关闭加减速：不产生任何步间隔延时 */
    TF_CHECK_INT(sim.delay_calls, 0);

    tf_end();

    /* ---------------- 加减速 ---------------- */
    tf_begin("stepper/加减速");
    {
        step_sim_t rs;
        stepper_t rst;
        stepper_ops_t rops;
        uint32_t calls_before;

        memset(&rs, 0, sizeof(rs));
        rops = ops;
        rops.ctx = &rs;

        TF_CHECK_INT(stepper_init(&rst, &rops), 0);
        TF_CHECK_INT(rst.max_speed, 0);
        TF_CHECK_INT(stepper_set_ramp(&rst, 1000U, 100U, 2000U, 2000U), 0);
        TF_CHECK_INT(rst.max_speed, 1000);
        TF_CHECK_INT(rst.start_speed, 100);
        TF_CHECK_INT(rst.accel, 2000);
        TF_CHECK_INT(rst.decel, 2000);

        TF_CHECK_INT(stepper_move(&rst, 400), 0);
        TF_CHECK_INT(rs.steps, 400);
        TF_CHECK_INT(rst.position, 400);
        TF_CHECK_INT(rs.delay_calls, 399);      /* 最后一步不等待 */
        TF_CHECK(rs.time_us > 0U);

        /* 起步 100 步/秒 → 10000us；中间加速变快，末段减速变慢 */
        TF_CHECK_INT(rs.first_us, 10000);
        TF_CHECK(rs.min_us < rs.first_us);
        TF_CHECK(rs.last_us > rs.min_us);
        TF_CHECK(rs.last_us >= 6000U && rs.last_us <= 12000U);

        /* 反向移动同样走加减速，位置正确回退 */
        TF_CHECK_INT(stepper_move(&rst, -100), 0);
        TF_CHECK_INT(rs.steps, 500);
        TF_CHECK_INT(rst.position, 300);
        TF_CHECK_INT(rst.direction, 1);
        TF_CHECK_INT(rs.delay_calls, 399 + 99);

        /* 关闭加减速：恢复定速发脉冲，不再产生延时 */
        calls_before = rs.delay_calls;
        TF_CHECK_INT(stepper_set_ramp(&rst, 0U, 0U, 0U, 0U), 0);
        TF_CHECK_INT(rst.max_speed, 0);
        TF_CHECK_INT(stepper_move(&rst, 10), 0);
        TF_CHECK_INT(rs.steps, 510);
        TF_CHECK_INT(rs.delay_calls, calls_before);
    }

    tf_end();

    /* ---------------- 硬件定时器加减速 ---------------- */
    tf_begin("stepper/硬件定时器加减速");
    {
        step_sim_t ts;
        stepper_t tst;
        stepper_ops_t tops;
        hal_tim_t tim;
        hal_tim_config_t tcfg;
        uint32_t min_period;

        memset(&ts, 0, sizeof(ts));
        tops = ops;
        tops.ctx = &ts;

        memset(&tcfg, 0, sizeof(tcfg));
        tcfg.prescaler = 0U;
        tcfg.period = 10000U;
        tcfg.counter_mode = HAL_TIM_COUNTER_UP;
        TF_CHECK_INT(hal_tim_init(&tim, &tcfg), HAL_OK);

        TF_CHECK_INT(stepper_init(&tst, &tops), 0);
        TF_CHECK_INT(stepper_set_ramp(&tst, 1000U, 100U, 2000U, 2000U), 0);

        /* 参数校验与绑定 */
        TF_CHECK_INT(stepper_set_timer(NULL, &tim, 1000000U), -1);
        TF_CHECK_INT(stepper_set_timer(&tst, &tim, 0U), -1);
        TF_CHECK_INT(stepper_set_timer(&tst, &tim, 1000000U), 0);
        TF_CHECK(tim.callback == stepper_tim_irq);
        TF_CHECK(tim.user_data == &tst);

        /* 速度更新间隔参数校验，默认 10ms */
        TF_CHECK_INT(stepper_set_ramp_interval(NULL, 1000U), -1);
        TF_CHECK_INT(stepper_set_ramp_interval(&tst, 0U), -1);
        TF_CHECK_INT(stepper_set_ramp_interval(&tst, 10000U), 0);
        TF_CHECK_INT(tst.ramp_interval_us, 10000U);

        /*
         * 定时器路径会阻塞等待 ramp_done：由低优先级任务发起移动，
         * 本任务手动触发定时器回调逐拍驱动（PC 平台 start_it 只同步触发一次）。
         * 1MHz 计数时钟、10ms 更新间隔：起步 100 步/秒（周期 10000，
         * 每区间 1 个脉冲）。S 曲线 27 档从 100 爬到 640（三角形曲线峰值）。
         */
        TF_CHECK_INT(tim_drive(&tst, &tim, 200, 400U), 0);
        TF_CHECK_INT(ts.steps, 200);
        TF_CHECK_INT(tst.position, 200);
        TF_CHECK_INT(tst.ramp_remaining, 0);
        TF_CHECK_INT(tst.ramp_status, 0);
        TF_CHECK_INT(tim.base.state, HAL_STATE_READY);   /* 结束由回调停表 */
        /* 减速提前起减：峰值约 500+ 步/秒（周期 <2000），结束已减回起步速度 */
        min_period = s_tim_min_period;
        TF_CHECK(min_period < 2500U);
        TF_CHECK(min_period >= 1000U);
        TF_CHECK_INT(tim.config.period, 10000U);

        /* 反向：同样由定时器驱动，位置回退 */
        TF_CHECK_INT(tim_drive(&tst, &tim, -100, 300U), 0);
        TF_CHECK_INT(ts.steps, 300);
        TF_CHECK_INT(tst.position, 100);
        TF_CHECK_INT(tst.direction, 1);
        TF_CHECK_INT(tst.ramp_remaining, 0);

        /* 底层发脉冲失败：回调内立即停表结束并返回错误 */
        ts.step_fail = 1;
        TF_CHECK_INT(tim_drive(&tst, &tim, 10, 200U), -1);
        ts.step_fail = 0;
        TF_CHECK_INT(tst.ramp_remaining, 0);
        TF_CHECK_INT(ts.steps, 300);
        TF_CHECK_INT(tim.base.state, HAL_STATE_READY);

        /* 1ms 更新间隔：曲线档数更细，同样完整走完 */
        TF_CHECK_INT(stepper_set_ramp_interval(&tst, 1000U), 0);
        TF_CHECK_INT(tim_drive(&tst, &tim, 150, 4000U), 0);
        TF_CHECK_INT(ts.steps, 450);
        TF_CHECK_INT(tst.position, 250);
        TF_CHECK_INT(tst.ramp_remaining, 0);
        TF_CHECK_INT(stepper_set_ramp_interval(&tst, 10000U), 0);

        /* 卸载定时器：加减速退回软件延时路径 */
        TF_CHECK_INT(stepper_set_timer(&tst, NULL, 0U), 0);
        TF_CHECK(tst.tim == NULL);
        TF_CHECK_INT(stepper_move(&tst, 50), 0);
        TF_CHECK_INT(ts.steps, 500);
        TF_CHECK(ts.delay_calls > 0U);

        TF_CHECK_INT(hal_tim_deinit(&tim), HAL_OK);
    }

    tf_end();
}
