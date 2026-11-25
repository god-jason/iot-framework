/*
 * app_stress.c - 内核/调度器稳定性压测
 * ============================================================================
 *
 * 目的：在真实硬件上长时间（默认 30 分钟）制造高切换密度与深调用，
 *       用来暴露调度器、栈、同步原语的问题。
 *
 * 任务构成（12 个，优先级 1..5，均低于 idle）：
 *   st_calc0..3  prio 3  14 层递归 + CRC32 + 整数开方 + 64 位乘加（结果自校验）
 *   st_mtx0/1    prio 4  互斥量竞争，持锁期间做 12 层递归，计数守恒校验
 *   st_qprod/qcons prio 5 队列生产者/消费者，序列号与 CRC 逐条校验
 *   st_sync0/1   prio 4  两个信号量乒乓，检验唤醒/阻塞不丢事件
 *   st_poll      prio 5  任务通知收发 + 短睡眠/让出轮转
 *   st_mon       prio 1  每秒检查心跳与不变量，每 10s 打印一次汇总
 *
 * 所有数学均为整数运算：本工程未保存 FPU 的 s16-s31，浮点跨任务不可靠。
 *
 * 输出示例：
 *   [I][st] up=60s loops=123456 err=0 heap=45678/65536
 *   [I][st] RESULT PASS: up=1800s loops=... err=0
 */

#include "app_stress.h"

#include "ros.h"
#include "log.h"
#include "crc32.h"

#include <string.h>

/* 压测时长：30 分钟；报告周期 10 秒；心跳停滞判定 15 秒 */
#define ST_RUN_MS          (30UL * 60UL * 1000UL)
#define ST_REPORT_MS       10000UL
#define ST_STALL_MS        15000UL

enum {
    ST_CALC0 = 0, ST_CALC1, ST_CALC2, ST_CALC3,
    ST_MTX0, ST_MTX1,
    ST_QPROD, ST_QCONS,
    ST_SYNC0, ST_SYNC1,
    ST_POLL,
    ST_MON,
    ST_TASK_COUNT
};

typedef struct {
    const char *name;
    uint8_t prio;
    uint16_t stack;
    volatile uint32_t loops;        /* 完成的工作轮数（心跳） */
    volatile uint32_t errors;       /* 本任务发现的错误数 */
    uint32_t last_loops;            /* 监控用：上秒心跳 */
    uint32_t stall_ms;              /* 监控用：连续无进展时长 */
    ros_task_t *task;
} st_stat_t;

static st_stat_t s_stat[ST_TASK_COUNT];

static const struct {
    const char *name;
    uint8_t prio;
    uint16_t stack;
} s_cfg[ST_TASK_COUNT] = {
    { "st_calc0", 3U, 2048U }, { "st_calc1", 3U, 2048U },
    { "st_calc2", 3U, 2048U }, { "st_calc3", 3U, 2048U },
    { "st_mtx0",  4U, 1536U }, { "st_mtx1",  4U, 1536U },
    { "st_qprod", 5U, 1024U }, { "st_qcons", 5U, 1024U },
    { "st_sync0", 4U, 1024U }, { "st_sync1", 4U, 1024U },
    { "st_poll",  5U, 1024U },
    { "st_mon",   1U, 1024U },
};

/* 所有任务创建完成后置 1，在此之前各自睡眠等待（避免打断 app_main 初始化） */
static volatile uint8_t s_running;
static uint8_t s_started;

/* ============================================================================
 * 计算负载
 * ==========================================================================*/

/* 递归 15 层（depth=14..0），每层 4 个局部变量 + 64 位累加，验证深栈 */
static uint32_t st_deep14(uint32_t depth, uint32_t seed)
{
    volatile uint32_t local[4];
    uint64_t acc = seed;

    local[0] = seed ^ 0xA5A5A5A5u;
    local[1] = local[0] * 2654435761u;
    local[2] = local[1] ^ (local[0] >> 7);
    local[3] = local[2] + local[1] + depth;

    acc = (uint64_t)local[3] * 0x100000001B3ull + local[0];
    if (depth == 0u) {
        return (uint32_t)(acc ^ (acc >> 32)) ^ local[0];
    }
    return st_deep14(depth - 1u, (uint32_t)(acc ^ local[1])) ^ local[3];
}

/* 整数牛顿迭代开方 */
static uint32_t st_isqrt32(uint32_t v)
{
    uint32_t x = v;
    uint32_t y = (x + 1u) >> 1;

    if (v == 0u) {
        return 0u;
    }
    while (y < x) {
        x = y;
        y = (x + v / x) >> 1;
    }
    return x;
}

/* 一轮完整计算：结果只依赖 id，可作为“预期值”反复校验 */
static uint32_t st_calc_once(uint32_t id)
{
    uint8_t buf[24];
    uint32_t i;
    uint32_t acc = 0x12345678u ^ (id * 0x9E3779B9u);

    for (i = 0u; i < 3u; i++) {
        acc ^= st_deep14(14u, acc + i);
    }
    for (i = 0u; i < (uint32_t)sizeof(buf); i++) {
        buf[i] = (uint8_t)(acc >> ((i & 3u) * 8u)) ^ (uint8_t)i;
    }
    acc ^= crc32(buf, (uint32_t)sizeof(buf));
    acc += st_isqrt32(acc | 1u);
    acc = (uint32_t)((uint64_t)acc * 6364136223846793005ull +
                     1442695040888963407ull);
    return acc;
}

/* ============================================================================
 * 共享对象
 * ==========================================================================*/

static ros_mutex_t s_mtx;
static volatile uint32_t s_mtx_value;          /* 持锁递增，应等于两任务次数之和 */
static volatile uint32_t s_mtx_inc[2];         /* 每个互斥任务的成功次数 */

typedef struct {
    uint32_t seq;
    uint32_t crc;
} st_msg_t;

static ros_queue_t s_queue;
static st_msg_t s_qbuf[8];
static volatile uint32_t s_q_sent;
static volatile uint32_t s_q_recv;

static ros_sem_t s_sem_a;
static ros_sem_t s_sem_b;
static volatile uint32_t s_sync_round;

static volatile uint32_t s_notify_seq;

/* ============================================================================
 * 任务入口
 * ==========================================================================*/

static void st_calc_entry(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    uint32_t expect = 0u;
    uint8_t have_expect = 0u;

    while (s_running == 0u) {
        ros_sleep_ms(5U);
    }

    for (;;) {
        uint32_t v = st_calc_once(idx);

        if (have_expect == 0u) {
            expect = v;
            have_expect = 1u;
        } else if (v != expect) {
            s_stat[idx].errors++;
        }
        s_stat[idx].loops++;
        ros_sleep_ms(1U);
    }
}

static void st_mutex_entry(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    uint32_t id = idx - ST_MTX0;

    while (s_running == 0u) {
        ros_sleep_ms(5U);
    }

    for (;;) {
        if (ros_mutex_lock(&s_mtx, 1000U) != ROS_OK) {
            s_stat[idx].errors++;      /* 超时：锁或调度异常 */
        } else {
            uint32_t v = s_mtx_value;

            (void)st_deep14(12u, v);   /* 持锁做深调用，拉长临界区 */
            s_mtx_value = v + 1u;
            (void)ros_mutex_unlock(&s_mtx);
            s_mtx_inc[id]++;
        }
        s_stat[idx].loops++;
        ros_sleep_ms(1U);
    }
}

static void st_queue_prod_entry(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    uint32_t seq = 0u;

    while (s_running == 0u) {
        ros_sleep_ms(5U);
    }

    for (;;) {
        st_msg_t m;

        m.seq = seq++;
        m.crc = m.seq * 2654435761u;
        if (ros_queue_send(&s_queue, &m, 100U) == ROS_OK) {
            s_q_sent++;
        } else {
            s_stat[idx].errors++;      /* 消费者失联：不应超时 */
        }
        s_stat[idx].loops++;
        if ((seq & 15u) == 0u) {
            ros_sleep_ms(1U);
        }
    }
}

static void st_queue_cons_entry(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    uint32_t expect = 0u;

    while (s_running == 0u) {
        ros_sleep_ms(5U);
    }

    for (;;) {
        st_msg_t m;

        if (ros_queue_recv(&s_queue, &m, 100U) != ROS_OK) {
            /* 队列空等超时属正常（生产/消费速率不同），只记录轮数 */
        } else {
            if (m.seq != expect || m.crc != m.seq * 2654435761u) {
                s_stat[idx].errors++;  /* 顺序错乱或数据损坏 */
            }
            expect = m.seq + 1u;
            s_q_recv++;
        }
        s_stat[idx].loops++;
    }
}

/* 两个任务用信号量乒乓：任一方向丢失都会以超时暴露 */
static void st_sync_entry(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    uint32_t id = idx - ST_SYNC0;
    ros_sem_t *mine = (id == 0u) ? &s_sem_a : &s_sem_b;
    ros_sem_t *peer = (id == 0u) ? &s_sem_b : &s_sem_a;

    while (s_running == 0u) {
        ros_sleep_ms(5U);
    }

    if (id == 0u) {
        (void)ros_sem_give(peer);      /* 由 0 号发起 */
    }

    for (;;) {
        if (ros_sem_take(mine, 1000U) != ROS_OK) {
            s_stat[idx].errors++;      /* 对端没把令牌送来 */
        } else {
            uint32_t v = st_deep14(11u, s_sync_round + id);

            if ((v & 1u) == 0u) {
                s_sync_round++;        /* 用结果参与共享计数，避免被优化掉 */
            } else {
                s_sync_round++;
            }
            (void)ros_sem_give(peer);
        }
        s_stat[idx].loops++;
        if ((s_sync_round & 63u) == 0u) {
            ros_sleep_ms(1U);
        }
    }
}

/* 通知 + 短睡眠 + 让出，混合负载 */
static void st_poll_entry(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg;

    while (s_running == 0u) {
        ros_sleep_ms(5U);
    }

    for (;;) {
        uint32_t value = 0u;
        int rc = ros_task_wait_notify(s_stat[idx].task, 1000U, &value);

        if (rc == ROS_OK) {
            if (value != s_notify_seq) {
                s_stat[idx].errors++;  /* 通知值不连续：丢了或串了 */
            }
        } else if (rc != ROS_TIMEOUT) {
            s_stat[idx].errors++;
        }
        s_stat[idx].loops++;

        (void)st_deep14(10u, value);
        if ((s_stat[idx].loops & 3u) == 0u) {
            ros_yield();
        } else {
            ros_sleep_ms(1U);
        }
    }
}

static uint32_t st_total_loops(void)
{
    uint32_t i;
    uint32_t sum = 0u;

    for (i = 0u; i < ST_TASK_COUNT; i++) {
        sum += s_stat[i].loops;
    }
    return sum;
}

static uint32_t st_total_errors(void)
{
    uint32_t i;
    uint32_t sum = 0u;

    for (i = 0u; i < ST_TASK_COUNT; i++) {
        sum += s_stat[i].errors;
    }
    return sum;
}

/* 监控：心跳停滞 + 互斥计数守恒 + 周期汇总 + 到期结论 */
static void st_monitor_entry(void *arg)
{
    uint32_t start;
    uint32_t next_report;
    uint32_t detail_at = 0u;
    uint8_t done = 0u;
    uint32_t i;

    (void)arg;
    while (s_running == 0u) {
        ros_sleep_ms(5U);
    }

    start = ros_tick_get();
    next_report = start + ST_REPORT_MS;

    for (;;) {
        uint32_t now;
        uint32_t elapsed;

        ros_sleep_ms(1000U);
        now = ros_tick_get();

        /* 1) 心跳：除监控自身外，15s 没有进展即视为卡死 */
        for (i = 0u; i < ST_TASK_COUNT; i++) {
            if (i == ST_MON) {
                continue;
            }
            if (s_stat[i].loops != s_stat[i].last_loops) {
                s_stat[i].last_loops = s_stat[i].loops;
                s_stat[i].stall_ms = 0u;
            } else {
                s_stat[i].stall_ms += 1000U;
                if (s_stat[i].stall_ms == ST_STALL_MS) {
                    s_stat[i].errors++;
                    LOG_E("st", "stall: task=%s loops=%u",
                          s_stat[i].name, (unsigned)s_stat[i].loops);
                }
            }
        }

        /* 2) 互斥量保护下的计数必须与两次递增之和一致 */
        if (ros_mutex_lock(&s_mtx, 50U) == ROS_OK) {
            uint32_t v = s_mtx_value;
            uint32_t a = s_mtx_inc[0];
            uint32_t b = s_mtx_inc[1];

            (void)ros_mutex_unlock(&s_mtx);
            if (v != a + b) {
                s_stat[ST_MTX0].errors++;
                s_stat[ST_MTX1].errors++;
                LOG_E("st", "mutex invariant: value=%u inc=%u+%u",
                      (unsigned)v, (unsigned)a, (unsigned)b);
            }
        } else {
            s_stat[ST_MTX0].errors++;
            LOG_E("st", "mutex monitor timeout");
        }

        /* 3) 通知通道：每秒一个递增序号 */
        s_notify_seq++;
        (void)ros_task_notify(s_stat[ST_POLL].task, s_notify_seq);

        elapsed = now - start;

        /* 4) 周期汇总 */
        if ((int32_t)(now - next_report) >= 0) {
            ros_mem_info_t mi;

            next_report += ST_REPORT_MS;
            (void)ros_mem_get_info(&mi);
            LOG_I("st", "up=%us loops=%u err=%u heap=%u/%u",
                  (unsigned)(elapsed / 1000U), (unsigned)st_total_loops(),
                  (unsigned)st_total_errors(), (unsigned)mi.free_bytes,
                  (unsigned)mi.total_bytes);

            if (elapsed >= detail_at + 60000U) {
                detail_at = elapsed;
                LOG_I("st", "c=%u,%u,%u,%u m=%u,%u q=%u,%u s=%u,%u p=%u",
                      (unsigned)s_stat[ST_CALC0].loops,
                      (unsigned)s_stat[ST_CALC1].loops,
                      (unsigned)s_stat[ST_CALC2].loops,
                      (unsigned)s_stat[ST_CALC3].loops,
                      (unsigned)s_stat[ST_MTX0].loops,
                      (unsigned)s_stat[ST_MTX1].loops,
                      (unsigned)s_stat[ST_QPROD].loops,
                      (unsigned)s_stat[ST_QCONS].loops,
                      (unsigned)s_stat[ST_SYNC0].loops,
                      (unsigned)s_stat[ST_SYNC1].loops,
                      (unsigned)s_stat[ST_POLL].loops);
            }
        }

        /* 5) 到期结论（只打印一次，之后继续低频心跳） */
        if (done == 0u && elapsed >= ST_RUN_MS) {
            uint32_t err = st_total_errors();

            done = 1u;
            LOG_I("st", "RESULT %s: up=%us loops=%u err=%u",
                  (err == 0u) ? "PASS" : "FAIL",
                  (unsigned)(elapsed / 1000U), (unsigned)st_total_loops(),
                  (unsigned)err);
        }
        if (done != 0u) {
            ros_sleep_ms(4000U);       /* 结论后降载，便于观察 shell */
        }
    }
}

/* ============================================================================
 * 启动
 * ==========================================================================*/

static const ros_entry_t s_entry[ST_TASK_COUNT] = {
    st_calc_entry, st_calc_entry, st_calc_entry, st_calc_entry,
    st_mutex_entry, st_mutex_entry,
    st_queue_prod_entry, st_queue_cons_entry,
    st_sync_entry, st_sync_entry,
    st_poll_entry,
    st_monitor_entry,
};

void app_stress_start(void)
{
    ros_mem_info_t mi;
    uint32_t created = 0u;
    uint32_t i;

    if (s_started != 0u) {
        return;
    }
    s_started = 1u;

    memset(s_stat, 0, sizeof(s_stat));
    s_mtx_value = 0u;
    s_mtx_inc[0] = 0u;
    s_mtx_inc[1] = 0u;
    s_q_sent = 0u;
    s_q_recv = 0u;
    s_sync_round = 0u;
    s_notify_seq = 0u;

    (void)ros_mutex_init(&s_mtx);
    (void)ros_sem_init(&s_sem_a, 0u);
    (void)ros_sem_init(&s_sem_b, 0u);
    (void)ros_queue_init(&s_queue, s_qbuf, (uint32_t)sizeof(st_msg_t), 8U);

    for (i = 0u; i < ST_TASK_COUNT; i++) {
        s_stat[i].name = s_cfg[i].name;
        s_stat[i].prio = s_cfg[i].prio;
        s_stat[i].stack = s_cfg[i].stack;
        s_stat[i].task = ros_task_create_dynamic(s_cfg[i].name, s_cfg[i].prio,
                                                 s_entry[i],
                                                 (void *)(uintptr_t)i,
                                                 s_cfg[i].stack);
        if (s_stat[i].task != NULL) {
            created++;
        }
    }

    (void)ros_mem_get_info(&mi);
    if (created != ST_TASK_COUNT) {
        LOG_E("st", "only %u/%u tasks created, heap free=%u",
              (unsigned)created, (unsigned)ST_TASK_COUNT,
              (unsigned)mi.free_bytes);
    } else {
        LOG_I("st", "start %u tasks, heap free=%u", (unsigned)created,
              (unsigned)mi.free_bytes);
    }

    s_running = 1u;                 /* 放行所有压测任务 */
}
