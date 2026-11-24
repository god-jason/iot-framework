/*
 * app_main.c - 单元测试入口（PC 应用）
 *
 * 本文件实现 app_main()，由 platforms/pc/pc_main.c 创建为独立任务；
 * 依次运行各模块测试套件并汇总结果，结束后 ros_stop() 返回 main，
 * 失败与否通过 app_set_exit_code() 传给进程退出码。
 */

#include "test.h"
#include "ros.h"
#include "app.h"

int tf_checks = 0;
int tf_failures = 0;

static const char *s_suite = "";
static int s_suite_failures = 0;

/* 开始一个测试套件：记录名称与当前失败计数基线。 */
void tf_begin(const char *name)
{
    s_suite = name;
    s_suite_failures = tf_failures;
}

/* 结束当前套件：根据失败计数增量输出 PASS/FAIL。 */
void tf_end(void)
{
    printf("%s  %s\n",
           (tf_failures == s_suite_failures) ? "PASS" : "FAIL",
           s_suite);
}

/* ---------------- 基础组件 ---------------- */
void ut_list(void);
void ut_strings(void);
void ut_map(void);
void ut_vec(void);
void ut_queue(void);
void ut_ringbuf(void);
void ut_fsm(void);
void ut_json(void);
void ut_log(void);

void ut_bytes(void);
void ut_bcd(void);
void ut_endian(void);
void ut_hex(void);

void ut_crc(void);
void ut_md5(void);
void ut_sha256(void);
void ut_base64(void);

void ut_datetime(void);
void ut_tlv(void);
void ut_statistics(void);
void ut_filter(void);
void ut_interp(void);
void ut_pid(void);
void ut_params(void);
void ut_cmd(void);
void ut_args(void);
void ut_scheduler(void);

/* ---------------- 通信与运维 ---------------- */
void ut_at(void);
void ut_at_parser(void);
void ut_at_urc(void);
void ut_storage(void);
void ut_fs(void);
void ut_mfs(void);
void ut_robot_proto(void);
void ut_can_proto(void);

/* ---------------- 运动控制 ---------------- */
void ut_motor(void);
void ut_servo(void);
void ut_stepper(void);
void ut_encoder(void);
void ut_kinematics(void);
void ut_odometry(void);
void ut_trajectory(void);
void ut_imu(void);

/* ---------------- 感知与决策 ---------------- */
void ut_perception(void);
void ut_localization(void);
void ut_robot_og_map(void);
void ut_robot_nav(void);
void ut_robot_dstar(void);
void ut_robot_rrt(void);
void ut_safety(void);

void ut_robot_action(void);
void ut_robot_bt(void);
void ut_robot_planner(void);
void ut_robot_executor(void);

/* ---------------- 其它组件 ---------------- */
void ut_breath_led(void);
void ut_fan(void);
void ut_roshell(void);
void ut_roshell_cmd(void);
void ut_net(void);
void ut_http(void);
void ut_mqtt(void);
void ut_fota(void);
void ut_config(void);

/* ---------------- 内核 ---------------- */
void ut_version(void);
void ut_trace(void);
void ut_ros(void);
void ut_dynamic(void);
void ut_io(void);
void ut_sbuf(void);
void ut_msgq(void);
void ut_rtc(void);

/* 测试入口：依次运行各模块套件，失败码交给板级 main。 */
void app_main(void *arg)
{
    (void)arg;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== robotos 单元测试 ===\n\n");

    printf("--- 基础组件 ---\n");
    ut_list();
    ut_strings();
    ut_map();
    ut_vec();
    ut_queue();
    ut_ringbuf();
    ut_fsm();
    ut_json();
    ut_log();
    ut_bytes();
    ut_bcd();
    ut_endian();
    ut_hex();
    ut_crc();
    ut_md5();
    ut_sha256();
    ut_base64();
    ut_datetime();
    ut_tlv();
    ut_statistics();
    ut_filter();
    ut_interp();
    ut_pid();
    ut_params();
    ut_cmd();
    ut_args();
    ut_scheduler();

    printf("\n--- 通信与运维 ---\n");
    ut_at();
    ut_at_parser();
    ut_at_urc();
    ut_storage();
    ut_fs();
    ut_mfs();
    ut_robot_proto();
    ut_can_proto();

    printf("\n--- 运动控制 ---\n");
    ut_motor();
    ut_servo();
    ut_stepper();
    ut_encoder();
    ut_kinematics();
    ut_odometry();
    ut_trajectory();
    ut_imu();

    printf("\n--- 感知与决策 ---\n");
    ut_perception();
    ut_localization();
    ut_robot_og_map();
    ut_robot_nav();
    ut_robot_dstar();
    ut_robot_rrt();
    ut_safety();
    ut_robot_action();
    ut_robot_bt();
    ut_robot_planner();
    ut_robot_executor();

    printf("\n--- 其它组件 ---\n");
    ut_breath_led();
    ut_fan();
    ut_roshell();
    ut_roshell_cmd();
    ut_net();
    ut_http();
    ut_mqtt();
    ut_fota();
    ut_config();

    printf("\n--- 内核 ---\n");
    ut_version();
    ut_ros();
    ut_trace();
    ut_dynamic();
    ut_io();
    ut_sbuf();
    ut_msgq();
    ut_rtc();

    printf("\n=== 结果：%d 项检查，%d 项失败 ===\n", tf_checks, tf_failures);

    app_set_exit_code((tf_failures == 0) ? 0 : 1);
    ros_stop();                 /* 结束调度，返回板级 main */
}
