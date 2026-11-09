/*
 * motion_error_test.c - 解释器错误记录测试（对应 motion_error.c）
 *
 * 覆盖：清空、设置（含默认描述）、格式化设置与截断、码转文本。
 */

#include "test.h"
#include "motion_error.h"

void ut_motion_error(void)
{
    motion_error_t err;

    tf_begin("motion_error/清空与设置");

    memset(&err, 0xFF, sizeof(err));
    motion_error_clear(&err);
    TF_CHECK_INT(err.code, MOTION_ERR_NONE);
    TF_CHECK_INT(err.line, 0);
    TF_CHECK_STR(err.message, "");

    motion_error_set(&err, MOTION_ERR_SYNTAX, 7U, NULL);
    TF_CHECK_INT(err.code, MOTION_ERR_SYNTAX);
    TF_CHECK_INT(err.line, 7);
    TF_CHECK(err.message[0] != '\0');       /* 自动使用默认描述 */

    motion_error_set(&err, MOTION_ERR_PARAMETER, 0U, "bad param");
    TF_CHECK_INT(err.code, MOTION_ERR_PARAMETER);
    TF_CHECK_STR(err.message, "bad param");

    /* NULL 目标安全 */
    motion_error_clear(NULL);
    motion_error_set(NULL, MOTION_ERR_SYNTAX, 1U, "x");
    TF_CHECK(1);

    tf_end();

    tf_begin("motion_error/格式化与截断");

    motion_error_setf(&err, MOTION_ERR_RUNTIME, 12U, "value=%d", 42);
    TF_CHECK_INT(err.line, 12);
    TF_CHECK(strstr(err.message, "42") != NULL);

    /* 超长描述必须被截断且以 '\0' 结尾 */
    motion_error_setf(&err, MOTION_ERR_RUNTIME, 1U, "%s",
                      "0123456789012345678901234567890123456789"
                      "0123456789012345678901234567890123456789");
    TF_CHECK(strlen(err.message) < MOTION_MAX_ERROR_MSG);
    TF_CHECK(err.message[MOTION_MAX_ERROR_MSG - 1U] == '\0');

    motion_error_setf(NULL, MOTION_ERR_RUNTIME, 1U, "x");
    TF_CHECK(1);

    tf_end();

    tf_begin("motion_error/码转文本");

    TF_CHECK_STR(motion_error_message(MOTION_ERR_NONE), "no error");
    TF_CHECK(motion_error_message(MOTION_ERR_SYNTAX) != NULL);
    TF_CHECK(motion_error_message(MOTION_ERR_PARAMETER) != NULL);
    TF_CHECK(strstr(motion_error_message(99999), "unknown") != NULL);

    tf_end();
}
