/*
 * safety_test.c - 安全状态测试（对应 safety.c）
 *
 * 覆盖：急停、故障上报与锁存、清除、限位双向检查与自动恢复、
 *       运动许可判定。
 */

#include "test.h"
#include "safety.h"

void ut_safety(void)
{
    safety_t s;

    tf_begin("safety/初始化与许可");

    TF_CHECK_INT(safety_init(NULL), -1);
    TF_CHECK_INT(safety_init(&s), 0);
    TF_CHECK_INT(safety_faults(&s), 0);
    TF_CHECK(safety_motion_allowed(&s));
    TF_CHECK(!s.estop);

    tf_end();

    tf_begin("safety/急停");

    TF_CHECK_INT(safety_estop(&s, true), 0);
    TF_CHECK(s.estop);
    TF_CHECK(!safety_motion_allowed(&s));
    TF_CHECK_INT(s.estop_count, 1);
    TF_CHECK_INT(safety_faults(&s) & SAFETY_FAULT_ESTOP, SAFETY_FAULT_ESTOP);

    /* 重复触发不重复计数 */
    TF_CHECK_INT(safety_estop(&s, true), 0);
    TF_CHECK_INT(s.estop_count, 1);

    TF_CHECK_INT(safety_estop(&s, false), 0);
    TF_CHECK(safety_motion_allowed(&s));
    TF_CHECK_INT(safety_estop(NULL, true), -1);

    tf_end();

    tf_begin("safety/故障上报与锁存");

    TF_CHECK_INT(safety_raise(&s, SAFETY_FAULT_OVERCURRENT, true), 0);
    TF_CHECK(!safety_motion_allowed(&s));
    TF_CHECK_INT(s.latched & SAFETY_FAULT_OVERCURRENT,
                 SAFETY_FAULT_OVERCURRENT);

    /* 锁定故障只能显式清除 */
    TF_CHECK_INT(safety_clear(&s, SAFETY_FAULT_OVERCURRENT), 0);
    TF_CHECK(safety_motion_allowed(&s));
    TF_CHECK_INT(s.latched, 0);

    /* 非锁定故障同样可清除 */
    TF_CHECK_INT(safety_raise(&s, SAFETY_FAULT_COMM, false), 0);
    TF_CHECK(!safety_motion_allowed(&s));
    TF_CHECK_INT(safety_clear(&s, SAFETY_FAULT_COMM), 0);
    TF_CHECK(safety_motion_allowed(&s));

    TF_CHECK_INT(safety_raise(&s, 0U, false), -1);
    TF_CHECK_INT(safety_raise(NULL, SAFETY_FAULT_USER, false), -1);
    TF_CHECK_INT(safety_clear(NULL, SAFETY_FAULT_USER), -1);

    tf_end();

    tf_begin("safety/限位检查");

    TF_CHECK_INT(safety_check_limits(&s, true, false) & SAFETY_FAULT_LIMIT_MIN,
                 SAFETY_FAULT_LIMIT_MIN);
    TF_CHECK(!safety_motion_allowed(&s));

    /* 限位释放后自动恢复 */
    TF_CHECK_INT(safety_check_limits(&s, false, false), 0);
    TF_CHECK(safety_motion_allowed(&s));

    TF_CHECK_INT(safety_check_limits(&s, false, true) & SAFETY_FAULT_LIMIT_MAX,
                 SAFETY_FAULT_LIMIT_MAX);
    TF_CHECK_INT(safety_check_limits(&s, false, false), 0);
    TF_CHECK(safety_motion_allowed(&s));

    /* 两侧同时触发视为接线/逻辑异常 */
    TF_CHECK_INT(safety_check_limits(&s, true, true) & SAFETY_FAULT_USER,
                 SAFETY_FAULT_USER);
    TF_CHECK(!safety_motion_allowed(&s));

    TF_CHECK_INT(safety_check_limits(NULL, true, false), 0);

    tf_end();
}
