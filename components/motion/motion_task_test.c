/* motion_task.c 单元测试：JOB 注册、启动、停止与状态。 */
#include "test.h"
#include "motion.h"
#include "motion_error.h"
#include "motion_task.h"

void ut_motion_task(void)
{
    tf_begin("motion_task/注册与查找");
    TF_CHECK_INT(motion_init(), 0);
    TF_CHECK_INT(motion_load(
                     "JOB FAN\n"
                     "WAIT TIME 0ms\n"
                     "ENDJOB\n"
                     "JOB PUMP\n"
                     "WAIT TIME 100ms\n"
                     "ENDJOB\n"),
                 0);
    TF_CHECK_INT(motion_task_count(), 2);
    TF_CHECK(motion_task_find("fan") >= 0);
    TF_CHECK(motion_task_find("PUMP") >= 0);
    TF_CHECK_INT(motion_task_find("nope"), -1);
    TF_CHECK_INT(motion_task_state("FAN"), MOTION_STATE_IDLE);
    tf_end();

    tf_begin("motion_task/启动完成与重启");
    TF_CHECK_INT(motion_task_start("fan"), 0);
    TF_CHECK_INT(motion_task_state("FAN"), MOTION_STATE_RUNNING);
    motion_task_poll();
    motion_task_poll();
    TF_CHECK_INT(motion_task_state("FAN"), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(motion_task_start("FAN"), 0);
    TF_CHECK_INT(motion_task_state("FAN"), MOTION_STATE_RUNNING);
    tf_end();

    tf_begin("motion_task/停止与非法操作");
    TF_CHECK_INT(motion_task_start("pump"), 0);
    TF_CHECK_INT(motion_task_stop("pump"), 0);
    TF_CHECK_INT(motion_task_state("PUMP"), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(motion_task_start("nope"), MOTION_ERR_PARAMETER);
    TF_CHECK_INT(motion_task_stop("nope"), MOTION_ERR_PARAMETER);
    tf_end();

    tf_begin("motion_task/重新加载清除任务");
    TF_CHECK_INT(motion_load("JOB X\nENDJOB\n"), 0);
    TF_CHECK_INT(motion_task_count(), 1);
    TF_CHECK(motion_task_find("X") >= 0);
    TF_CHECK_INT(motion_task_find("FAN"), -1);
    tf_end();
}
