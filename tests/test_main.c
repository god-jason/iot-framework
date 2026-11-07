/* 单元测试入口：依次运行各模块测试套件。 */
#include "test_framework.h"

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

void test_crypto(void);
void test_utils(void);
void test_container(void);
void test_algorithm(void);
void test_datetime(void);
void test_modbus(void);
void test_mqtt(void);
void test_http(void);

int main(void)
{
    printf("=== iot-frame 单元测试 ===\n\n");

    test_crypto();
    printf("\n");
    test_utils();
    printf("\n");
    test_container();
    printf("\n");
    test_algorithm();
    printf("\n");
    test_datetime();
    printf("\n");
    test_modbus();
    printf("\n");
    test_mqtt();
    printf("\n");
    test_http();

    printf("\n=== 结果：%d 项检查，%d 项失败 ===\n", tf_checks, tf_failures);
    return (tf_failures == 0) ? 0 : 1;
}
