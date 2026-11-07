/* motion_vm.c 单元测试：虚拟机状态机、错误处理与变量复位。 */
#include "test.h"
#include "motion_error.h"
#include "motion_source.h"
#include "motion_variable.h"
#include "motion_vm.h"

void ut_motion_vm(void)
{
    motion_vm_t vm;
    motion_source_t src;
    motion_value_t value;
    int i;

    tf_begin("motion_vm/参数与初始状态");
    motion_vm_init(&vm);
    TF_CHECK_INT(vm.state, MOTION_STATE_IDLE);
    TF_CHECK_INT(motion_vm_load(NULL, &src), MOTION_ERR_PARAMETER);
    TF_CHECK_INT(motion_vm_load(&vm, NULL), MOTION_ERR_PARAMETER);
    TF_CHECK_INT(motion_vm_start(&vm), MOTION_ERR_PARAMETER);
    tf_end();

    tf_begin("motion_vm/启动暂停恢复停止终止");
    motion_vm_init(&vm);
    motion_source_string_init(&src, "WAIT TIME 50ms\n");
    TF_CHECK_INT(motion_vm_load(&vm, &src), 0);
    TF_CHECK_INT(motion_vm_start(&vm), 0);
    TF_CHECK_INT(motion_vm_get_state(&vm), MOTION_STATE_RUNNING);
    TF_CHECK_INT(motion_vm_pause(&vm), 0);
    TF_CHECK_INT(motion_vm_get_state(&vm), MOTION_STATE_PAUSED);
    TF_CHECK_INT(motion_vm_resume(&vm), 0);
    TF_CHECK_INT(motion_vm_get_state(&vm), MOTION_STATE_RUNNING);
    motion_vm_poll(&vm);
    TF_CHECK_INT(motion_vm_get_state(&vm), MOTION_STATE_WAITING);
    TF_CHECK_INT(motion_vm_stop(&vm), 0);
    TF_CHECK_INT(motion_vm_get_state(&vm), MOTION_STATE_COMPLETED);
    TF_CHECK_INT(motion_vm_start(&vm), 0);
    TF_CHECK_INT(motion_vm_abort(&vm), 0);
    TF_CHECK_INT(motion_vm_get_state(&vm), MOTION_STATE_ABORTED);
    tf_end();

    tf_begin("motion_vm/变量复位与执行");
    motion_vm_init(&vm);
    motion_source_string_init(&src,
                              "SET $A = 1\n"
                              "SET $A = $A + 1\n");
    motion_vm_load(&vm, &src);
    memset(&value, 0, sizeof(value));
    value.type = MOTION_VALUE_INTEGER;
    value.data.integer = 9;
    motion_variable_set(&vm.variables, "A", &value);
    TF_CHECK_INT(motion_vm_start(&vm), 0);
    for (i = 0; i < 5 && vm.state == MOTION_STATE_RUNNING; i++) {
        motion_vm_poll(&vm);
    }
    TF_CHECK_INT(vm.state, MOTION_STATE_COMPLETED);
    TF_CHECK(motion_variable_get(&vm.variables, "A", &value));
    TF_CHECK_INT(value.data.integer, 2);
    tf_end();

    tf_begin("motion_vm/ERROR 指令进入错误态");
    motion_vm_init(&vm);
    motion_source_string_init(&src, "ERROR 1008\n");
    motion_vm_load(&vm, &src);
    TF_CHECK_INT(motion_vm_start(&vm), 0);
    motion_vm_poll(&vm);
    TF_CHECK_INT(vm.state, MOTION_STATE_ERROR);
    TF_CHECK_INT(vm.error.code, MOTION_ERR_RUNTIME);
    motion_vm_poll(&vm);
    TF_CHECK_INT(vm.state, MOTION_STATE_ERROR);
    tf_end();
}
