/* algorithm/motion 单元测试：梯形速度曲线加速/减速/到达与参数边界。 */
#include "test_framework.h"
#include "motion.h"

void ut_motion(void)
{
    motion_profile_t p;

    tf_begin("algorithm/motion 初始化与默认参数");
    memset(&p, 0xff, sizeof(p));
    TF_CHECK_INT(motion_init(&p), 0);
    TF_CHECK_NEAR(p.position, 0.0f, 1e-6);
    TF_CHECK_NEAR(p.velocity, 0.0f, 1e-6);
    TF_CHECK_NEAR(p.max_velocity, 1.0f, 1e-6);
    TF_CHECK_NEAR(p.max_acceleration, 1.0f, 1e-6);
    TF_CHECK_INT(p.active, 0);
    TF_CHECK(motion_finished(&p) == true);
    TF_CHECK_INT(motion_init(NULL), -1);
    TF_CHECK(motion_finished(NULL) == true);
    tf_end();

    tf_begin("algorithm/motion 目标设置与非法更新");
    motion_init(&p);
    TF_CHECK_INT(motion_set_target(&p, 5.0f), 0);
    TF_CHECK_INT(p.active, 1);
    TF_CHECK(motion_finished(&p) == false);
    TF_CHECK_INT(motion_set_target(NULL, 1.0f), -1);
    TF_CHECK_INT(motion_update(&p, 0.0f), -1);
    TF_CHECK_INT(motion_update(&p, -1.0f), -1);
    TF_CHECK_INT(motion_update(NULL, 1.0f), -1);

    motion_init(&p);
    TF_CHECK_INT(motion_update(&p, 1.0f), -1);
    tf_end();

    tf_begin("algorithm/motion 正向梯形运动到达");
    motion_init(&p);
    motion_set_target(&p, 5.0f);
    TF_CHECK_INT(motion_update(&p, 1.0f), 0);
    TF_CHECK_NEAR(p.position, 1.0f, 1e-5);
    TF_CHECK_NEAR(p.velocity, 1.0f, 1e-5);
    TF_CHECK_INT(motion_update(&p, 1.0f), 0);
    TF_CHECK_NEAR(p.position, 2.0f, 1e-5);
    TF_CHECK_INT(motion_update(&p, 1.0f), 0);
    TF_CHECK_NEAR(p.position, 3.0f, 1e-5);
    TF_CHECK_INT(motion_update(&p, 1.0f), 0);
    TF_CHECK_NEAR(p.position, 4.0f, 1e-5);
    TF_CHECK_INT(motion_update(&p, 1.0f), 0);
    TF_CHECK_NEAR(p.position, 5.0f, 1e-5);
    TF_CHECK_NEAR(p.velocity, 0.0f, 1e-6);
    TF_CHECK(motion_finished(&p) == true);
    TF_CHECK_INT(motion_update(&p, 1.0f), -1);
    tf_end();

    tf_begin("algorithm/motion 负向运动");
    motion_init(&p);
    motion_set_target(&p, -3.0f);
    TF_CHECK_INT(motion_update(&p, 1.0f), 0);
    TF_CHECK_NEAR(p.position, -1.0f, 1e-5);
    TF_CHECK_INT(motion_update(&p, 1.0f), 0);
    TF_CHECK_NEAR(p.position, -2.0f, 1e-5);
    TF_CHECK_INT(motion_update(&p, 1.0f), 0);
    TF_CHECK_NEAR(p.position, -3.0f, 1e-5);
    TF_CHECK(motion_finished(&p) == true);
    tf_end();

    tf_begin("algorithm/motion 大步长不越过目标");
    motion_init(&p);
    motion_set_target(&p, 5.0f);
    TF_CHECK_INT(motion_update(&p, 10.0f), 0);
    TF_CHECK_NEAR(p.position, 5.0f, 1e-5);
    TF_CHECK_NEAR(p.velocity, 0.0f, 1e-6);
    TF_CHECK_NEAR(p.acceleration, 0.0f, 1e-6);
    TF_CHECK(motion_finished(&p) == true);
    tf_end();

    tf_begin("algorithm/motion 最大速度限制");
    motion_init(&p);
    p.max_velocity = 0.5f;
    motion_set_target(&p, 10.0f);
    TF_CHECK_INT(motion_update(&p, 1.0f), 0);
    TF_CHECK_NEAR(p.velocity, 0.5f, 1e-5);
    TF_CHECK_NEAR(p.position, 0.5f, 1e-5);
    tf_end();

    tf_begin("algorithm/motion 减速段速度归零");
    motion_init(&p);
    p.position = 0.0f;
    p.velocity = 1.0f;
    motion_set_target(&p, 0.5f);
    TF_CHECK_INT(motion_update(&p, 1.0f), 0);
    TF_CHECK_NEAR(p.position, 0.0f, 1e-5);
    TF_CHECK_NEAR(p.velocity, 0.0f, 1e-6);
    TF_CHECK(motion_finished(&p) == false);
    TF_CHECK_INT(motion_update(&p, 1.0f), 0);
    TF_CHECK_NEAR(p.position, 0.5f, 1e-5);
    TF_CHECK(motion_finished(&p) == true);
    tf_end();

    tf_begin("algorithm/motion 完成后重新设定目标");
    motion_init(&p);
    motion_set_target(&p, 1.0f);
    motion_update(&p, 1.0f);
    TF_CHECK(motion_finished(&p) == true);
    TF_CHECK_INT(motion_set_target(&p, 0.0f), 0);
    TF_CHECK_INT(p.active, 1);
    TF_CHECK_INT(motion_update(&p, 1.0f), 0);
    TF_CHECK_NEAR(p.position, 0.0f, 1e-5);
    TF_CHECK(motion_finished(&p) == true);
    tf_end();
}
