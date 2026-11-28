#ifndef APP_MEMTEST_H
#define APP_MEMTEST_H

/*
 * app_memtest.h - F407 堆内存泄漏自检
 *
 * APP_MEMTEST_ENABLE = 1：编译并在 app_main 启动阶段同步执行一次；
 * APP_MEMTEST_ENABLE = 0：不参与编译（app_main 中的调用被预处理掉）。
 *
 * 自检内容：var/var_json 复杂结构、vec、map、str_dup、裸 malloc、
 * 动态任务创建/销毁；以「本任务 mem_used 必须回到基线」为主判据，
 * 全局堆 used/free 以容差为辅判据。
 *
 * 放在压测/文件系统启动之前同步执行，避免其它任务并发分配造成干扰。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 默认关闭：自检会临时占用堆与启动时间，仅调试目标开启 */
#ifndef APP_MEMTEST_ENABLE
#define APP_MEMTEST_ENABLE 0
#endif

/* 同步执行一次泄漏自检并打印 PASS/FAIL；重复调用无副作用 */
void app_memtest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MEMTEST_H */
