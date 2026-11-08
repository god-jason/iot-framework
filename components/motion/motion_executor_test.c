/* motion_executor.c 单元测试：指令分发、表达式与设备错误路径。 */
#include "test.h"
#include "motion_device.h"
#include "motion_error.h"
#include "motion_flow.h"
#include "motion_source.h"
#include "motion_variable.h"
#include "motion_vm.h"
#include "pc.h"

static void run_vm(motion_vm_t *vm, int max_polls)
{
    int i;

    for (i = 0; i < max_polls; i++) {
        if (motion_vm_get_state(vm) == MOTION_STATE_COMPLETED ||
            motion_vm_get_state(vm) == MOTION_STATE_ERROR ||
            motion_vm_get_state(vm) == MOTION_STATE_ABORTED) {
            break;
        }
        motion_vm_poll(vm);
    }
}

void ut_motion_executor(void)
{
    motion_vm_t vm;
    motion_source_t src;
    motion_value_t value;

    tf_begin("motion_executor/表达式赋值与条件");
    motion_vm_init(&vm);
    motion_source_string_init(&src,
                              "SET $A = 1\n"
                              "SET $B = $A + 2\n"
                              "IF $B == 3\n"
                              "SET $C = TRUE\n"
                              "END\n");
    TF_CHECK_INT(motion_flow_build(src.text), 0);
    TF_CHECK_INT(motion_vm_load(&vm, &src), 0);
    TF_CHECK_INT(motion_vm_start(&vm), 0);
    run_vm(&vm, 10);
    TF_CHECK_INT(motion_vm_get_state(&vm), MOTION_STATE_COMPLETED);
    TF_CHECK(motion_variable_get(&vm.variables, "B", &value));
    TF_CHECK_INT(value.data.integer, 3);
    TF_CHECK(motion_variable_get(&vm.variables, "C", &value));
    TF_CHECK(value.data.boolean);
    tf_end();

    tf_begin("motion_executor/WAIT TIME 调度");
    motion_vm_init(&vm);
    motion_source_string_init(&src, "WAIT TIME 10ms\n");
    motion_vm_load(&vm, &src);
    motion_vm_start(&vm);
    motion_vm_poll(&vm);
    TF_CHECK_INT(motion_vm_get_state(&vm), MOTION_STATE_WAITING);
    iot_test_tick_advance(11U);
    motion_vm_poll(&vm);
    TF_CHECK_INT(motion_vm_get_state(&vm), MOTION_STATE_COMPLETED);
    tf_end();

    tf_begin("motion_executor/设备未注册错误");
    motion_device_reset();
    motion_vm_init(&vm);
    motion_source_string_init(&src, "GPIO 0 ON\n");
    motion_vm_load(&vm, &src);
    motion_vm_start(&vm);
    motion_vm_poll(&vm);
    TF_CHECK_INT(motion_vm_get_state(&vm), MOTION_STATE_ERROR);
    TF_CHECK_INT(vm.error.code, MOTION_ERR_DEVICE);
    tf_end();

    tf_begin("motion_executor/参数与语法错误");
    motion_vm_init(&vm);
    motion_source_string_init(&src, "GPIO 0 SET\n");
    motion_vm_load(&vm, &src);
    motion_vm_start(&vm);
    motion_vm_poll(&vm);
    TF_CHECK_INT(motion_vm_get_state(&vm), MOTION_STATE_ERROR);
    TF_CHECK_INT(vm.error.code, MOTION_ERR_PARAMETER);

    motion_vm_init(&vm);
    motion_source_string_init(&src, "FOO BAR\n");
    motion_vm_load(&vm, &src);
    motion_vm_start(&vm);
    motion_vm_poll(&vm);
    TF_CHECK_INT(motion_vm_get_state(&vm), MOTION_STATE_ERROR);
    TF_CHECK_INT(vm.error.code, MOTION_ERR_SYNTAX);
    tf_end();
}
