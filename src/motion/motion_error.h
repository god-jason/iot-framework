#ifndef MOTION_ERROR_H
#define MOTION_ERROR_H

/*
 * motion_error.h —— 错误码与错误信息记录接口。
 *
 * 职责：统一定义解释器错误码，并提供"清零 / 设置 / 格式化设置 /
 *       码转文本"四个基础操作，供各模块上报故障。
 *
 * 数据流：出错模块调用 motion_error_set() 或 motion_error_setf()
 *         把码、行号、描述写入调用方的 motion_error_t；
 *         上层读取 code/line/message 决定是否进入 ON ERROR 分支。
 *
 * 边界约束：message 为定长 MOTION_MAX_ERROR_MSG 字节，写入必截断并
 *           补 '\0'；本模块不分配内存、不持有全局状态。
 */

#include "motion_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* §15 预定义错误码 + 解释器通用错误码 */
typedef enum {
    MOTION_ERR_NONE = 0,     /* 无错误 */

    MOTION_ERR_SYNTAX = 1,   /* 语法错误 */
    MOTION_ERR_FLOW = 2,     /* 流程控制错误（分支/循环不匹配等） */
    MOTION_ERR_STACK_OVERFLOW = 3, /* 调用栈或嵌套深度溢出 */
    MOTION_ERR_VARIABLE = 4, /* 变量未定义或变量表满 */
    MOTION_ERR_RESOURCE_BUSY = 5,  /* 设备/资源被占用 */
    MOTION_ERR_UNSUPPORTED = 6,    /* 不支持的操作 */
    MOTION_ERR_MEMORY = 7,   /* 内存不足 */

    MOTION_ERR_PARAMETER = 1001,   /* 参数非法 */
    MOTION_ERR_DEVICE = 1002,      /* 设备不存在或未注册 */
    MOTION_ERR_DEVICE_EXEC = 1003, /* 设备回调执行失败 */
    MOTION_ERR_TIMEOUT = 1004,     /* 等待超时 */
    MOTION_ERR_SENSOR = 1005,      /* 传感器错误 */
    MOTION_ERR_MOTOR = 1006,       /* 电机错误 */
    MOTION_ERR_LIMIT = 1007,       /* 触发限位 */
    MOTION_ERR_RUNTIME = 1008      /* 其他运行时错误 */
} motion_error_code_t;

/* 清空错误记录：code 归 0，line 归 0，message 置空。error 可为 NULL。 */
void motion_error_clear(motion_error_t *error);
/* 设置错误记录。
 * 参数：error 目标结构（可为 NULL）；code 错误码；line 出错行号（0 表示未知）；
 *       message 描述文本，为 NULL 或空串时自动使用 motion_error_message(code)。 */
void motion_error_set(motion_error_t *error, int code, uint32_t line,
                      const char *message);
/* 以 printf 风格格式化设置错误描述。
 * 参数：error 目标结构；code 错误码；line 出错行号；fmt 格式串，为空时
 *       使用 motion_error_message(code)。
 * 说明：格式化结果超长时自动截断，并保证以 '\0' 结尾。 */
void motion_error_setf(motion_error_t *error, int code, uint32_t line,
                       const char *fmt, ...);
/* 把错误码转换为静态英文描述；未知码返回 "unknown error"。 */
const char *motion_error_message(int code);

#ifdef __cplusplus
}
#endif

#endif /* MOTION_ERROR_H */
