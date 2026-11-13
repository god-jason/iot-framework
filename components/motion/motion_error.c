#include "motion_error.h"
#include "ros.h"
#include "common.h"

/*
 * motion_error.c —— 错误码与错误信息记录实现。
 *
 * 模块职责：提供错误结构体的清零、普通设置、格式化设置与错误码转文本。
 *
 * 数据流：任意模块 -> motion_error_set/setf -> motion_error_t
 *         -> 执行层读取 code/line/message 并决定是否跳转 ON ERROR。
 *
 * 边界约束：所有写入都以 MOTION_MAX_ERROR_MSG 为界并手工补 '\0'；
 *           本文件不使用静态可变状态，可重入；不分配内存。
 */

#include <stdio.h>
#include <stdarg.h>

/* 清零错误记录：整体 memset，再显式把 code 置为 NONE。
 * 参数：error 目标结构，可为 NULL（此时静默返回）。 */
void motion_error_clear(motion_error_t *error)
{
    if (error == NULL) {
        return;
    }
    memset(error, 0, sizeof(*error));
    error->code = MOTION_ERR_NONE;
}

/* 设置错误记录（描述为固定文本）。
 * 参数：error 目标结构；code 错误码；line 行号；message 描述，可为 NULL。
 * 返回：无。message 为空时回退到内置描述。 */
void motion_error_set(motion_error_t *error, int code, uint32_t line,
                      const char *message)
{
    if (error == NULL) {
        return;
    }

    error->code = code;
    error->line = line;
    error->message[0] = '\0';

    if (message != NULL && message[0] != '\0') {
        (void)str_copy(error->message, sizeof(error->message), message);
    } else {
        /* 未提供描述时使用错误码对应的默认文本 */
        (void)str_copy(error->message, sizeof(error->message),
                       motion_error_message(code));
    }
}

/* 设置错误记录（描述为 printf 风格格式化文本）。
 * 参数：error 目标结构；code 错误码；line 行号；fmt 格式串，可为 NULL。
 * 返回：无。vsnprintf 保证不越界，末尾再强制补 '\0' 防止截断边界问题。 */
void motion_error_setf(motion_error_t *error, int code, uint32_t line,
                       const char *fmt, ...)
{
    va_list args;

    if (error == NULL) {
        return;
    }

    error->code = code;
    error->line = line;
    error->message[0] = '\0';

    if (fmt != NULL && fmt[0] != '\0') {
        va_start(args, fmt);
        (void)vsnprintf(error->message, sizeof(error->message), fmt, args);
        va_end(args);
        error->message[sizeof(error->message) - 1U] = '\0';
    } else {
        (void)str_copy(error->message, sizeof(error->message),
                       motion_error_message(code));
    }
}

/* 错误码转静态描述文本。
 * 参数：code 错误码。
 * 返回：指向常量字符串的指针，调用方不得修改或释放。 */
const char *motion_error_message(int code)
{
    switch (code) {
    case MOTION_ERR_NONE:
        return "no error";
    case MOTION_ERR_SYNTAX:
        return "syntax error";
    case MOTION_ERR_FLOW:
        return "flow control error";
    case MOTION_ERR_STACK_OVERFLOW:
        return "stack overflow";
    case MOTION_ERR_VARIABLE:
        return "variable error";
    case MOTION_ERR_RESOURCE_BUSY:
        return "resource busy";
    case MOTION_ERR_UNSUPPORTED:
        return "unsupported operation";
    case MOTION_ERR_MEMORY:
        return "out of memory";
    case MOTION_ERR_PARAMETER:
        return "parameter error";
    case MOTION_ERR_DEVICE:
        return "device not found";
    case MOTION_ERR_DEVICE_EXEC:
        return "device execution failed";
    case MOTION_ERR_TIMEOUT:
        return "wait timeout";
    case MOTION_ERR_SENSOR:
        return "sensor error";
    case MOTION_ERR_MOTOR:
        return "motor error";
    case MOTION_ERR_LIMIT:
        return "limit triggered";
    case MOTION_ERR_RUNTIME:
        return "runtime error";
    default:
        return "unknown error";
    }
}
