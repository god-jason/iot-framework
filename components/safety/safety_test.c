/* safety 单元测试：急停、故障锁存、限位与运动许可。 */
#include "test.h"
#include "safety.h"

void ut_safety(void)
{
    safety_t safety;

    tf_begin("safety/初始化与运动许可");
    TF_CHECK_INT(safety_init(NULL), -1);
    TF_CHECK_INT(safety_init(&safety), 0);
    TF_CHECK_INT(safety_faults(&safety), 0);
    TF_CHECK(safety_motion_allowed(&safety));
    tf_end();

    tf_begin("safety/急停状态");
    TF_CHECK_INT(safety_estop(&safety, true), 0);
    TF_CHECK(safety.estop);
    TF_CHECK(!safety_motion_allowed(&safety));
    TF_CHECK_INT(safety.estop_count, 1);
    TF_CHECK_INT(safety_faults(&safety) & SAFETY_FAULT_ESTOP, SAFETY_FAULT_ESTOP);
    TF_CHECK_INT(safety_estop(&safety, false), 0);
    TF_CHECK(safety_motion_allowed(&safety));
    tf_end();

    tf_begin("safety/锁定故障需显式清除");
    TF_CHECK_INT(safety_raise(&safety, SAFETY_FAULT_OVERCURRENT, true), 0);
    TF_CHECK(!safety_motion_allowed(&safety));
    TF_CHECK_INT(safety.latched & SAFETY_FAULT_OVERCURRENT, SAFETY_FAULT_OVERCURRENT);
    TF_CHECK_INT(safety_clear(&safety, SAFETY_FAULT_OVERCURRENT), 0);
    TF_CHECK(safety_motion_allowed(&safety));
    TF_CHECK_INT(safety_raise(&safety, 0U, false), -1);
    tf_end();

    tf_begin("safety/限位自动恢复");
    TF_CHECK_INT(safety_check_limits(&safety, true, false) &
                 SAFETY_FAULT_LIMIT_MIN, SAFETY_FAULT_LIMIT_MIN);
    TF_CHECK(!safety_motion_allowed(&safety));
    TF_CHECK_INT(safety_check_limits(&safety, false, false), 0);
    TF_CHECK(safety_motion_allowed(&safety));
    TF_CHECK_INT(safety_check_limits(&safety, true, true) & SAFETY_FAULT_USER,
                 SAFETY_FAULT_USER);
    tf_end();
}
