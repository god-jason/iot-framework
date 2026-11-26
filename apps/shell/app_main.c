/*
 * app_main.c - PC 平台 roshell 交互示例
 *
 * 本文件实现 app_main()，由 platforms/pc/pc_main.c 创建为独立任务。
 * rs_platform_start() 完成默认绑定：
 *   - 输出：stdout（与 log 的 printf 同一路）
 *   - 输入：监听任务逐字节读 stdin；未激活时只匹配 hello 口令，
 *     激活后投喂 roshell 会话
 * 上电只跑监听任务，输入 hello 回车才创建 roshell 实例与输入任务；
 * 输入 exit 回收实例回到监听；管道 EOF 或 quit 命令后停止内核。
 *
 * 额外创建两个测试任务，用于验证 ps / state 的统计：
 *   cpu_load : 每轮把模拟时间推进 3 tick 再睡 15 tick → CPU 约 25%
 *   mem_load : 逐秒 +512B 再逐秒 -512B（每块持有 1s）→ ps 可见
 *
 * 本目标定义了 ROS_PC_REAL_TIME：模拟时间跟随墙钟，1 tick ≈ 1ms 真实时间，
 * 因此上面说的"秒"就是现实中的秒。
 *
 * 交互测试：
 *   xmake r ros-shell
 *   > hello          （开启口令，回复 hello!）
 *   > ps
 *   > state
 *   > help
 *   > echo hello roshell
 *   > $x = 40 + 2            （脚本行：赋值给 roshell 变量）
 *   > echo $x                （echo 做 $var 替换 → 42）
 *   > if $x > 40 then echo big end
 *   > run "for $i = 1 to 3 echo $i end"   （后台 run，jobs 查看）
 *   > jobs                   （查看 run 槽位）
 *   > waitjob 0              （等待槽位 0 结束）
 *   > gpio mode PA5 out      （设备命令走 cmd 全局命令表）
 *   > gpio set PA5 1
 *   > fan 0 speed 60%
 *   > ping 127.0.0.1 count 4
 *   > exit           （回收会话，回到口令监听）
 * 管道测试：
 *   "hello`nps`nstate`nquit`n" | ros-shell.exe
 */

#include "roshell.h"
#include "gpio.h"
#include "hal.h"
#include "fan.h"
#include "ros.h"
#include "common.h"
#include "app.h"
#include "app_fs.h"

#include <stdio.h>

static int cmd_quit(roshell_t *rs, int argc, char **argv,
                    const rs_args_parsed_t *parsed, void *user)
{
    (void)rs;
    (void)argc;
    (void)argv;
    (void)parsed;
    (void)user;
    app_fs_deinit();
    ros_puts("bye");
    ros_stop();
    return 0;
}

static const rs_cmd_t s_quit_def = {
    "quit", "stop the shell demo", NULL, NULL, NULL, 0U, cmd_quit, NULL,
    NULL, NULL, NULL
};

/* ------------------------------ 测试任务 ------------------------------ */

static ros_task_t s_cpu_task;
static uint8_t    s_cpu_stack[1024];

static ros_task_t s_mem_task;
static uint8_t    s_mem_stack[1024];

/* 演示用 PWM 通道：由应用准备句柄，风扇实例直接使用 */
static hal_pwm_t s_demo_pwm[2];

/* 演示用风扇：把 PWM 通道包装成 fan 实例，fan 命令里用 fan 0 / fan 1 操作 */
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

void app_main(void *arg)
{
    (void)arg;

    if (rs_platform_start("pc-demo") != 0) {
        printf("roshell start failed\n");
        app_set_exit_code(1);
        return;
    }
    (void)gpio_cmd_register();
    (void)app_fs_init(APP_FS_IMG_PATH);
    if (rs_cmd_register(&s_quit_def) != 0) {
        printf("register quit failed\n");
    }

    /* 准备两路 PWM 演示通道（PC 平台为模拟实现），并包装成风扇实例 */
    {
        hal_pwm_config_t pcfg = {
            .id = 0U,
            .channel = 1U,
            .frequency = 1000U,
            .duty = 5000U,
            .polarity = HAL_PWM_POLARITY_HIGH
        };
        uint32_t i;

        for (i = 0U; i < 2U; i++) {
            pcfg.channel = (uint8_t)(i + 1U);
            pcfg.id = (uint8_t)i;
            if (hal_pwm_init(&s_demo_pwm[i], &pcfg) != HAL_OK) {
                continue;
            }

            /* 同一个 PWM 通道再包装成风扇实例（fan_init 会注册 fan 命令） */
            s_demo_fan_ops[i].set_duty = demo_fan_duty;
            s_demo_fan_ops[i].ctx = &s_demo_pwm[i];
            (void)fan_init(&s_demo_fan[i], &s_demo_fan_ops[i]);
        }
    }

    (void)ros_task_create(&s_cpu_task, "cpu_load", 3U, cpu_load_entry, NULL,
                          s_cpu_stack, sizeof(s_cpu_stack));
    (void)ros_task_create(&s_mem_task, "mem_load", 5U, mem_load_entry, NULL,
                          s_mem_stack, sizeof(s_mem_stack));
}
