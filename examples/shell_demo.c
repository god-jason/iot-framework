/*
 * shell_demo.c - PC 平台 IoT Shell 交互示例
 *
 * shell_platform_start() 完成默认绑定：
 *   - 输出：stdout（与 log 的 printf 同一路）
 *   - 输入：默认 stdio（pc_io.c）逐字节读入；监听任务只匹配 hello 口令
 * 与 STM32 一致：上电只跑监听任务，输入 hello 回车才创建 Shell 实例；
 * 输入 exit 回收实例回到监听；管道 EOF 或 quit 命令后停止内核。
 * 输入结束（管道 EOF）或 quit 命令后停止内核，进程正常返回。
 *
 * 额外创建两个测试任务，用于验证 ps / state 的统计：
 *   cpu_load : 每轮把模拟时间推进 3 tick 再睡 15 tick → CPU 约 25%
 *   mem_load : 逐秒 +512B 再逐秒 -512B（每块持有 1s）→ ps 的 MEM 列阶梯变化
 *
 * 本目标定义了 ROS_PC_REAL_TIME：模拟时间跟随墙钟，1 tick ≈ 1ms 真实时间，
 * 因此上面说的"秒"就是现实中的秒。
 *
 * 交互测试：
 *   xmake r ros-shell
 *   > hello          （开启口令，回复 hello!）
 *   > ps
 *   > state
 *   > gpio mode PA5 out
 *   > gpio set PA5 1
 *   > pwm list                （[0] pwm1 / [1] pwm2）
 *   > pwm 0 duty 25%          （按槽位序号）
 *   > pwm pwm2 duty 75%       （按绑定名）
 *   > stepper 0 init PA6 PA7  （实例 0：step / dir 引脚）
 *   > stepper 0 speed 200
 *   > stepper 0 dir 1
 *   > stepper 0 move 100
 *   > -stepper 0 move 100     （行首 -：后台执行，立即回到提示符）
 *   > jobs                    （查看后台 JOB，stop / waitjob 管理）
 *   > stepper list
 *   > fan list                （[0] fan0 / [1] fan1）
 *   > fan 0 speed 60%         （等价 fan 0 speed 600）
 *   > fan 1 on
 *   > fan 0 rpm
 *   > fan 0 off
 *   > ping 127.0.0.1 count 4     （走 components/net 的 net_ping）
 *   > -ping 8.8.8.8 count 10     （后台 ping，jobs 可查看）
 *   > exit           （回收 Shell，回到口令监听）
 * 管道测试：
 *   "hello`nps`nstate`nquit`n" | ros-shell.exe
 */

#include "shell.h"
#include "shell_dev.h"
#include "fan.h"
#include "ros.h"

#include <stdio.h>

static int cmd_quit(shell_t *sh, int argc, char **argv)
{
    (void)sh;
    (void)argc;
    (void)argv;
    ros_puts("bye");
    ros_stop();
    return SHELL_OK;
}

/* ------------------------------ 测试任务 ------------------------------ */

static ros_task_t s_cpu_task;
static uint8_t    s_cpu_stack[1024];

static ros_task_t s_mem_task;
static uint8_t    s_mem_stack[1024];

/* 演示用 PWM 通道：由应用准备句柄并绑定名字，Shell 里用 pwm1/pwm2 或 0/1 操作 */
static hal_pwm_t s_demo_pwm[2];

/* 演示用风扇：把 PWM 通道包装成 fan 实例，Shell 里用 fan 0 / fan 1 操作 */
static fan_t     s_demo_fan[2];
static fan_ops_t s_demo_fan_ops[2];

static int demo_fan_duty(void *ctx, uint16_t duty)
{
    hal_pwm_t *pwm = (hal_pwm_t *)ctx;

    return (hal_pwm_set_duty(pwm, (uint32_t)duty * 10U) == HAL_OK) ? 0 : -1;
}

/*
 * CPU 负载：模拟端的时间只在 idle 里推进，所以这里用 ros_tick_increment()
 * 代表"本任务忙了 3 个 tick"，再真实睡眠 15 tick，占用约 25%。
 * 上板时换成一个真实的忙等/计算循环即可，统计口径一致。
 */
static void cpu_load_entry(void *arg)
{
    (void)arg;

    for (;;) {
        ros_tick_increment(3U);
        ros_sleep_ms(15U);
    }
}

/* 内存负载：持有 2KB 两秒，然后释放两秒，循环往复 */
static void mem_load_entry(void *arg)
{
    void *blocks[4];
    int i;

    (void)arg;

    for (;;) {
        for (i = 0; i < 4; i++) {
            blocks[i] = ros_mem_malloc(512U);
            ros_sleep_ms(1000U);
        }
        for (i = 0; i < 4; i++) {
            ros_mem_free(blocks[i]);
            blocks[i] = NULL;
            ros_sleep_ms(1000U);
        }
        ros_sleep_ms(2000U);
    }
}

int main(void)
{
    static const shell_cmd_t quit_cmd = {
        "quit", cmd_quit, "quit", "stop the shell demo", "demo", CMD_NONE
    };

    ros_init();
    if (shell_platform_start("pc-demo") != 0) {
        printf("shell start failed\n");
        return 1;
    }
    if (shell_register_cmd(&quit_cmd) != 0) {
        printf("register quit failed\n");
    }

    /* 准备两路 PWM 并绑定给 Shell（PC 平台为模拟实现） */
    {
        static const hal_pwm_config_t pcfg = {
            1000U, 5000U, HAL_PWM_POLARITY_HIGH
        };
        uint32_t i;

        for (i = 0U; i < 2U; i++) {
            char name[SHELL_PWM_NAME_MAX];

            s_demo_pwm[i].channel = (uint8_t)(i + 1U);
            if (hal_pwm_init(&s_demo_pwm[i], &pcfg) != HAL_OK) {
                continue;
            }
            (void)snprintf(name, sizeof(name), "pwm%u", (unsigned)(i + 1U));
            (void)shell_pwm_bind(name, &s_demo_pwm[i]);

            /* 同一个 PWM 通道再包装成风扇实例 */
            s_demo_fan_ops[i].set_duty = demo_fan_duty;
            s_demo_fan_ops[i].ctx = &s_demo_pwm[i];
            if (fan_init(&s_demo_fan[i], &s_demo_fan_ops[i]) == 0) {
                (void)shell_fan_bind((uint8_t)i, &s_demo_fan[i]);
            }
        }
    }

    (void)ros_task_create(&s_cpu_task, "cpu_load", 3U, cpu_load_entry, NULL,
                          s_cpu_stack, sizeof(s_cpu_stack));
    (void)ros_task_create(&s_mem_task, "mem_load", 5U, mem_load_entry, NULL,
                          s_mem_stack, sizeof(s_mem_stack));

    ros_sched_start();          /* ros_stop() 后返回 */

    printf("shell demo finished\n");
    return 0;
}
