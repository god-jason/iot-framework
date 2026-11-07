/* 单元测试入口：依次运行各模块测试套件。 */
#include "test.h"

int tf_checks = 0;
int tf_failures = 0;

static const char *s_suite = "";
static int s_suite_failures = 0;

void tf_begin(const char *name)
{
    s_suite = name;
    s_suite_failures = tf_failures;
}

void tf_end(void)
{
    printf("%s  %s\n",
           (tf_failures == s_suite_failures) ? "PASS" : "FAIL",
           s_suite);
}

/* ---------------- core ---------------- */
void ut_cJSON(void);
void ut_base64(void);
void ut_bcd(void);
void ut_bytes(void);
void ut_console(void);
void ut_scheduler(void);
void ut_datetime(void);
void ut_endian(void);
void ut_hex(void);
void ut_log(void);
void ut_json(void);
void ut_shell(void);
void ut_fsm(void);

/* ---------------- crypto ---------------- */
void ut_crc(void);
void ut_md5(void);
void ut_sha256(void);

/* ---------------- container ---------------- */
void ut_list(void);
void ut_queue(void);
void ut_ringbuf(void);

/* ---------------- boot ---------------- */
void ut_boot(void);
void ut_image(void);
void ut_partition(void);
void ut_upgrade(void);

/* ---------------- algorithm ---------------- */
void ut_pid(void);
void ut_filter(void);
void ut_interp(void);
void ut_motion(void);
void ut_motion_source(void);
void ut_motion_parser(void);
void ut_motion_variable(void);
void ut_motion_flow(void);
void ut_motion_error(void);
void ut_motion_device(void);
void ut_motion_executor(void);
void ut_motion_vm(void);
void ut_motion_task(void);
void ut_statistics(void);

/* ---------------- robot ---------------- */
void ut_robot_action(void);
void ut_robot_bt(void);
void ut_robot_executor(void);
void ut_robot_planner(void);
void ut_robot_og_map(void);
void ut_robot_dstar(void);
void ut_robot_nav(void);
void ut_robot_rrt(void);

/* ---------------- components ---------------- */
void ut_breath_led(void);
void ut_encoder(void);
void ut_fan(void);
void ut_fota(void);
void ut_motor(void);
void ut_servo(void);
void ut_stepper(void);
void ut_kv(void);
void ut_storage(void);

/* ---------------- modem / ml307 ---------------- */
void ut_at(void);
void ut_at_parser(void);
void ut_at_urc(void);
void ut_http(void);
void ut_ml307(void);
void ut_ml307_http(void);
void ut_ml307_mqtt(void);
void ut_modbus(void);
void ut_modbus_master(void);
void ut_modbus_slave(void);
void ut_tlv(void);
void ut_mqtt(void);

/* ---------------- port ---------------- */
void ut_port_mem(void);
void ut_port_os(void);
void ut_port_sim_bus(void);
void ut_port_sim_analog(void);
void ut_port_sim_timer(void);
void ut_port_sim_sys(void);
void ut_port_sim_optional(void);

/* ---------------- drivers ---------------- */
void ut_drivers(void);

/* ---------------- vendor ---------------- */
void ut_tlsf(void);
void ut_arena(void);

/* ---------------- 历史聚合测试 ---------------- */
void test_mqtt(void);
void test_http(void);

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== iot-frame 单元测试 ===\n\n");

    printf("--- core ---\n");
    ut_cJSON();
    ut_base64();
    ut_bcd();
    ut_bytes();
    ut_console();
    ut_scheduler();
    ut_datetime();
    ut_endian();
    ut_hex();
    ut_log();
    ut_json();
    ut_shell();
    ut_fsm();

    printf("\n--- crypto ---\n");
    ut_crc();
    ut_md5();
    ut_sha256();

    printf("\n--- container ---\n");
    ut_list();
    ut_queue();
    ut_ringbuf();

    printf("\n--- boot ---\n");
    ut_boot();
    ut_image();
    ut_partition();
    ut_upgrade();

    printf("\n--- algorithm ---\n");
    ut_pid();
    ut_filter();
    ut_interp();
    ut_motion();
    ut_motion_source();
    ut_motion_parser();
    ut_motion_variable();
    ut_motion_flow();
    ut_motion_error();
    ut_motion_device();
    ut_motion_executor();
    ut_motion_vm();
    ut_motion_task();
    ut_statistics();

    printf("\n--- robot ---\n");
    ut_robot_action();
    ut_robot_bt();
    ut_robot_executor();
    ut_robot_planner();
    ut_robot_og_map();
    ut_robot_dstar();
    ut_robot_nav();
    ut_robot_rrt();

    printf("\n--- components ---\n");
    ut_breath_led();
    ut_encoder();
    ut_fan();
    ut_fota();
    ut_motor();
    ut_servo();
    ut_stepper();
    ut_kv();
    ut_storage();

    printf("\n--- modem / ml307 ---\n");
    ut_at();
    ut_at_parser();
    ut_at_urc();
    ut_http();
    ut_ml307();
    ut_ml307_http();
    ut_ml307_mqtt();
    ut_modbus();
    ut_modbus_master();
    ut_modbus_slave();
    ut_tlv();
    ut_mqtt();

    printf("\n--- port ---\n");
    ut_port_mem();
    ut_port_os();
    ut_port_sim_bus();
    ut_port_sim_analog();
    ut_port_sim_timer();
    ut_port_sim_sys();
    ut_port_sim_optional();

    printf("\n--- drivers ---\n");
    ut_drivers();

    printf("\n--- vendor ---\n");
    ut_tlsf();
    ut_arena();

    printf("\n--- 历史聚合测试 ---\n");
    test_mqtt();
    test_http();

    printf("\n=== 结果：%d 项检查，%d 项失败 ===\n", tf_checks, tf_failures);
    return (tf_failures == 0) ? 0 : 1;
}
