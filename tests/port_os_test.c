/* PORT OS 适配接口单元测试（使用 simulator 提供的 ops）。 */
#include "test_framework.h"
#include "port_sim.h"

void ut_port_os(void)
{
    const port_os_ops_t *ops;
    const port_os_ops_t *saved;
    port_mutex_t mutex;
    port_semaphore_t sem;
    uint32_t t0;

    port_sim_reset_state();
    TF_CHECK_INT(port_platform_install(), PORT_OK);
    TF_CHECK_INT(port_init(), PORT_OK);

    tf_begin("port/os 获取与注册");
    ops = port_os_get();
    TF_CHECK(ops != NULL);
    saved = ops;
    TF_CHECK_INT(port_os_register(NULL), PORT_INVALID_PARAM);
    TF_CHECK(port_os_get() == saved);
    tf_end();

    tf_begin("port/os mutex");
    mutex = 0U;
    TF_CHECK_INT(ops->mutex_create(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(ops->mutex_create(&mutex), PORT_OK);
    TF_CHECK(mutex != 0U);

    TF_CHECK_INT(ops->mutex_lock(mutex, 0U), PORT_OK);
    TF_CHECK_INT(ops->mutex_unlock(mutex), PORT_OK);
    /* count 为 0 时 unlock 仍返回成功 */
    TF_CHECK_INT(ops->mutex_unlock(mutex), PORT_OK);

    TF_CHECK_INT(ops->mutex_lock(0U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(ops->mutex_unlock(0U), PORT_INVALID_PARAM);

    TF_CHECK_INT(ops->mutex_delete(mutex), PORT_OK);
    TF_CHECK_INT(ops->mutex_delete(mutex), PORT_INVALID_PARAM);
    TF_CHECK_INT(ops->mutex_lock(mutex, 0U), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("port/os semaphore");
    sem = 0U;
    TF_CHECK_INT(ops->semaphore_create(NULL, 4U, 2U), PORT_INVALID_PARAM);
    TF_CHECK_INT(ops->semaphore_create(&sem, 0U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(ops->semaphore_create(&sem, 2U, 3U), PORT_INVALID_PARAM);

    TF_CHECK_INT(ops->semaphore_create(&sem, 2U, 1U), PORT_OK);
    TF_CHECK(sem != 0U);
    TF_CHECK_INT(ops->semaphore_wait(sem, 0U), PORT_OK);
    TF_CHECK_INT(ops->semaphore_wait(sem, 0U), PORT_TIMEOUT);
    TF_CHECK_INT(ops->semaphore_post(sem), PORT_OK);
    TF_CHECK_INT(ops->semaphore_wait(sem, 0U), PORT_OK);
    /* 超过 max_count 的 post 仍返回成功（计数被钳制） */
    TF_CHECK_INT(ops->semaphore_post(sem), PORT_OK);
    TF_CHECK_INT(ops->semaphore_post(sem), PORT_OK);
    TF_CHECK_INT(ops->semaphore_post(sem), PORT_OK);

    TF_CHECK_INT(ops->semaphore_wait(0U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(ops->semaphore_post(0U), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("port/os tick、延时与临界区");
    t0 = ops->tick_get();
    ops->delay_ms(7U);
    TF_CHECK_INT(ops->tick_get() - t0, 7U);

    ops->critical_enter();
    ops->critical_exit();
    TF_CHECK(ops->critical_enter != NULL);
    TF_CHECK(ops->critical_exit != NULL);
    tf_end();

    tf_begin("port/os 恢复原 ops");
    TF_CHECK_INT(port_os_register(saved), PORT_OK);
    TF_CHECK(port_os_get() == saved);
    tf_end();
}
