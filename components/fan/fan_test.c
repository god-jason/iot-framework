/*
 * fan_test.c - 风扇测试（对应 fan.c）
 *
 * 覆盖：初始化归零、档位设置与钳位、全速/停止、NULL 安全。
 */

#include "test.h"
#include "fan.h"

static int s_last_duty;

static int sim_set_duty(void *ctx, uint16_t duty)
{
    (void)ctx;
    s_last_duty = (int)duty;
    return 0;
}

void ut_fan(void)
{
    fan_t fan;
    fan_ops_t ops;

    ops.set_duty = sim_set_duty;
    ops.ctx = NULL;

    tf_begin("fan/初始化");

    TF_CHECK_INT(fan_init(NULL, &ops), -1);
    TF_CHECK_INT(fan_init(&fan, NULL), -1);
    {
        fan_ops_t bad;
        bad.set_duty = NULL;
        bad.ctx = NULL;
        TF_CHECK_INT(fan_init(&fan, &bad), -1);
    }

    s_last_duty = -1;
    TF_CHECK_INT(fan_init(&fan, &ops), 0);
    TF_CHECK_INT(s_last_duty, 0);        /* 初始化即关闭 */
    TF_CHECK_INT(fan.duty, 0);

    tf_end();

    tf_begin("fan/档位与钳位");

    TF_CHECK_INT(fan_set_speed(&fan, 300U), 0);
    TF_CHECK_INT(s_last_duty, 300);
    TF_CHECK_INT(fan.duty, 300);

    TF_CHECK_INT(fan_set_speed(&fan, 1000U), 0);
    TF_CHECK_INT(s_last_duty, 1000);

    TF_CHECK_INT(fan_set_speed(&fan, 5000U), 0);   /* 超限钳位 */
    TF_CHECK_INT(s_last_duty, 1000);
    TF_CHECK_INT(fan.duty, 1000);

    TF_CHECK_INT(fan_on(&fan), 0);
    TF_CHECK_INT(s_last_duty, 1000);

    TF_CHECK_INT(fan_off(&fan), 0);
    TF_CHECK_INT(s_last_duty, 0);
    TF_CHECK_INT(fan.duty, 0);

    tf_end();

    tf_begin("fan/NULL 安全");

    TF_CHECK_INT(fan_set_speed(NULL, 100U), -1);
    TF_CHECK_INT(fan_on(NULL), -1);
    TF_CHECK_INT(fan_off(NULL), -1);

    tf_end();
}
