#ifndef MOTION_CONFIG_H
#define MOTION_CONFIG_H

/*
 * MotionScript 解释器静态资源配置。
 * 所有容量均为编译期常量，解释器不使用动态内存。
 *
 * 说明：这些宏决定各静态数组的尺寸，修改后会同时影响 RAM 占用与
 *       语法上限；因为被多个头文件包含，只能放在本文件集中维护。
 */

#define MOTION_MAX_JOBS              16   /* 可同时存在的 JOB 数量上限 */
#define MOTION_MAX_LINE              256  /* 单行源代码（含结尾 '\0'）的最大长度 */
#define MOTION_MAX_PARAMS            8    /* 单条指令可携带的参数键值对数量上限 */
#define MOTION_MAX_VARIABLES         64   /* 变量表中变量的最大条数 */
#define MOTION_MAX_LABELS            128  /* 标签表容量 */
#define MOTION_MAX_CALL_DEPTH        8    /* CALL 子流程的最大嵌套层数 */
#define MOTION_MAX_LOOP_DEPTH        8    /* LOOP 循环的最大嵌套层数 */
#define MOTION_MAX_IF_DEPTH          8    /* IF 分支的最大嵌套层数 */
#define MOTION_MAX_EXEC_PER_POLL     10   /* 单次轮询最多连续执行的指令条数，防止饿死其他任务 */
#define MOTION_MAX_STRING            32   /* 字符串字面量的最大字节数（含 '\0'） */
#define MOTION_MAX_JOB_NAME          24   /* JOB 名称的最大字节数（含 '\0'） */
#define MOTION_MAX_LABEL_NAME        24   /* 标签名称的最大字节数（含 '\0'） */
#define MOTION_MAX_VAR_NAME          24   /* 变量名称的最大字节数（含 '\0'） */
#define MOTION_MAX_PENDING_WAIT      4    /* 可同时挂起的等待条件数量上限 */

#define MOTION_MAX_PROGRAM           8192 /* 源程序文本缓冲的最大字节数（含 '\0'） */
#define MOTION_MAX_FLOW_BLOCKS       128  /* 流程块（IF/LOOP/JOB/ERROR 等）记录容量 */
#define MOTION_MAX_LINES             1024 /* 可缓存的源代码行数上限 */
#define MOTION_MAX_FLOW_DEPTH        32   /* 流程嵌套的总深度上限 */

#define MOTION_MAX_DEVICES           16   /* 设备号的上限（合法 id 为 0 ~ 15） */
#define MOTION_MAX_JOB_DEVICES       8    /* 单个 JOB 可占用设备的数量上限 */
#define MOTION_MAX_ADC_AVG           64   /* ADC 多次采样的最大平均次数 */

#define MOTION_MAX_ERROR_MSG         64   /* 错误描述文本的最大字节数（含 '\0'） */

/* 设备等待未显式指定 TIMEOUT 时默认不超时（0 表示无限等待）。 */
#define MOTION_DEFAULT_WAIT_TIMEOUT  0

#endif /* MOTION_CONFIG_H */
