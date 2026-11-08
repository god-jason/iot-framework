/* motion_error.c 单元测试：错误对象与错误码文本。 */
#include "test.h"
#include "motion_error.h"

void ut_motion_error(void)
{
    motion_error_t error;

    tf_begin("motion_error/清空与设置");
    motion_error_clear(&error);
    TF_CHECK_INT(error.code, MOTION_ERR_NONE);
    TF_CHECK_INT(error.line, 0);
    TF_CHECK_STR(error.message, "");
    motion_error_set(&error, MOTION_ERR_TIMEOUT, 12U, "wait timeout");
    TF_CHECK_INT(error.code, MOTION_ERR_TIMEOUT);
    TF_CHECK_INT(error.line, 12);
    TF_CHECK_STR(error.message, "wait timeout");
    motion_error_set(&error, MOTION_ERR_FLOW, 3U, NULL);
    TF_CHECK_INT(error.code, MOTION_ERR_FLOW);
    TF_CHECK_STR(error.message, motion_error_message(MOTION_ERR_FLOW));
    tf_end();

    tf_begin("motion_error/格式化与文本");
    motion_error_setf(&error, MOTION_ERR_DEVICE, 4U, "dev %d fail", 7);
    TF_CHECK_INT(error.code, MOTION_ERR_DEVICE);
    TF_CHECK_STR(error.message, "dev 7 fail");
    TF_CHECK_STR(motion_error_message(MOTION_ERR_NONE), "no error");
    TF_CHECK_STR(motion_error_message(MOTION_ERR_SYNTAX),
                 "syntax error");
    TF_CHECK_STR(motion_error_message(9999), "unknown error");
    tf_end();
}
